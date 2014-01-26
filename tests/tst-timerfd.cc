/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
// To compile on Linux, use: g++ -g -pthread -std=c++11 tests/tst-timerfd.cc

#include <sys/timerfd.h>
#include <poll.h>
#include <unistd.h>
#include <iostream>
#include <chrono>
#include <thread>

#include <errno.h>
int tests = 0, fails = 0;

template<typename T>
bool do_expect(T actual, T expected, const char *actuals, const char *expecteds, const char *file, int line)
{
    ++tests;
    if (actual != expected) {
        fails++;
        std::cout << "FAIL: " << file << ":" << line << ": For " << actuals
                << " expected " << expecteds << "(" << expected << "), saw "
                << actual << ".\n";
        return false;
    }
    std::cout << "OK: " << file << ":" << line << ".\n";
    return true;
}
template<typename T>
bool do_expectge(T actual, T expected, const char *actuals, const char *expecteds, const char *file, int line)
{
    ++tests;
    if (actual < expected) {
        fails++;
        std::cout << "FAIL: " << file << ":" << line << ": For " << actuals
                << " expected >=" << expecteds << ", saw " << actual << ".\n";
        return false;
    }
    std::cout << "OK: " << file << ":" << line << ".\n";
    return true;
}
#define expect(actual, expected) do_expect(actual, expected, #actual, #expected, __FILE__, __LINE__)
#define expectge(actual, expected) do_expectge(actual, expected, #actual, #expected, __FILE__, __LINE__)
#define expect_errno(call, experrno) ( \
        do_expect((long)(call), (long)-1, #call, "-1", __FILE__, __LINE__) && \
        do_expect(errno, experrno, #call " errno",  #experrno, __FILE__, __LINE__) )
#define expect_success(var, call) \
        errno = 0; \
        var = call; \
        do_expectge(var, 0, #call, "0", __FILE__, __LINE__); \
        do_expect(errno, 0, #call " errno",  "0", __FILE__, __LINE__);

inline constexpr long long operator"" _ms(unsigned long long t)
{
    return t * 1000000;
}

using u64 = uint64_t;

void dotest(int clockid)
{
    int fd;
    expect_success(fd, timerfd_create(clockid, TFD_NONBLOCK));
    int junk;

    // Before we set a timer, timerfd has no events for polling or reading
    pollfd pfd = { fd, POLLIN };
    expect(poll(&pfd, 1, 0), 0);
    char buf[1024];
    expect_errno(read(fd, buf, sizeof(buf)), EAGAIN);
    // Reading less than 8 bytes result in an error
    expect_errno(read(fd, buf, 7), EINVAL);

    // Set a timer to expire in 500ms from now (relative time)
    itimerspec t1 {{0,0},{0,500_ms}};
    expect_success(junk, timerfd_settime(fd, 0, &t1, nullptr));
    // If we check now, the timer should have not yet expired
    expect_errno(read(fd, buf, sizeof(buf)), EAGAIN);
    // But if we sleep for a whole second, the timer would have expired and read returns a counter of 1
    sleep(1);
    u64 counter;
    expect(read(fd, &counter, sizeof(counter)), (ssize_t)8);
    expect(counter, (u64)1);
    expect_errno(read(fd, &counter, sizeof(counter)), EAGAIN);

    // Similarly, set a timer to expire in 300ms, and then again every 200ms.
    // After 400ms, poll sees the timer expired at least once, but we do not read.
    // After 400ms more, we read and see the timer expired 3 times.
    itimerspec t2 {{0,200_ms},{0,300_ms}};
    expect_success(junk, timerfd_settime(fd, 0, &t2, nullptr));
    expect_errno(read(fd, buf, sizeof(buf)), EAGAIN);
    usleep(400000);
    expect(poll(&pfd, 1, 0), 1);
    expect((int)pfd.revents, POLLIN);
    usleep(400000);
    expect(poll(&pfd, 1, 0), 1);
    expect((int)pfd.revents, POLLIN);
    expect(read(fd, &counter, sizeof(counter)), (ssize_t)8);
    expect(counter, (u64)3);
    expect_errno(read(fd, &counter, sizeof(counter)), EAGAIN);

    // If sleep (so counter becomes nonzero again) and then cancel the
    // timer, the counter remains zero
    usleep(400000);
    expect(poll(&pfd, 1, 0), 1); // counter is nonzero
    itimerspec t3 {{0,0},{0,0}};
    expect_success(junk, timerfd_settime(fd, 0, &t3, nullptr));
    expect(poll(&pfd, 1, 0), 0); // counter is back to zero
    usleep(400000);
    expect(poll(&pfd, 1, 0), 0); // and timer was really canceled

    // Check absolute time setting
    // Set a timer to expire in 300ms from now (using absolute time)
    itimerspec t4 {};
    clock_gettime(clockid, &(t4.it_value));
    t4.it_value.tv_nsec += 300_ms;
    if (t4.it_value.tv_nsec >= 1000_ms) {
        t4.it_value.tv_nsec -= 1000_ms;
        t4.it_value.tv_sec++;
    }
    expect_success(junk, timerfd_settime(fd, TFD_TIMER_ABSTIME, &t4, nullptr));
    expect_errno(read(fd, buf, sizeof(buf)), EAGAIN);
    usleep(500000);
    expect(read(fd, &counter, sizeof(counter)), (ssize_t)8);
    expect(counter, (u64)1);
    expect_errno(read(fd, &counter, sizeof(counter)), EAGAIN);

    // Check blocking poll - simple case, no interval (see more complex
    // cases in blocking read tests below).
    itimerspec t45 {{0,0},{0,400_ms}};
    expect_success(junk, timerfd_settime(fd, 0, &t45, nullptr));
    auto before = std::chrono::high_resolution_clock::now();
    expect(poll(&pfd, 1, 0), 0);
    expect(poll(&pfd, 1, 100/*ms*/), 0);
    expect(poll(&pfd, 1, 1000/*ms*/), 1);
    auto after = std::chrono::high_resolution_clock::now();
    expectge((int64_t)std::chrono::duration_cast<std::chrono::nanoseconds> (after-before).count(), (int64_t)300_ms);

    // Check close
    expect_success(junk, close(fd));

    // Check that it's not a disaster to close an fd with a yet-unexpired timer
    expect_success(fd, timerfd_create(CLOCK_REALTIME, 0));
    pfd.fd = fd;
    itimerspec t46 {{0,0},{10,0}};
    expect_success(junk, timerfd_settime(fd, 0, &t46, nullptr));
    expect(poll(&pfd, 1, 0), 0);
    expect_success(junk, close(fd));


    // Open again with blocking read enabled
    expect_success(fd, timerfd_create(CLOCK_REALTIME, 0));
    pfd.fd = fd;

    // Check blocking read, no interval, timer set before read blocks
    itimerspec t5 {{0,0},{0,500_ms}};
    expect_success(junk, timerfd_settime(fd, 0, &t5, nullptr));
    before = std::chrono::high_resolution_clock::now();
    expect(read(fd, &counter, sizeof(counter)), (ssize_t)8);
    expect(counter, (u64)1);
    after = std::chrono::high_resolution_clock::now();
    expectge((int64_t)std::chrono::duration_cast<std::chrono::nanoseconds> (after-before).count(), (int64_t)400_ms);

    // Check blocking read, no interval, timer set after read blocks
    std::thread th2([&] {
        u64 counter2;
        before = std::chrono::high_resolution_clock::now();
        expect(read(fd, &counter2, sizeof(counter2)), (ssize_t)8);
        expect(counter2, (u64)1);
        after = std::chrono::high_resolution_clock::now();
        expectge((int64_t)std::chrono::duration_cast<std::chrono::nanoseconds> (after-before).count(), (int64_t)500_ms);
    });
    usleep(400000);
    itimerspec t6 {{0,0},{0,200_ms}};
    expect_success(junk, timerfd_settime(fd, 0, &t6, nullptr));
    th2.join();

    // Check blocking read, with interval
    itimerspec t7 {{0,200_ms},{0,500_ms}};
    expect_success(junk, timerfd_settime(fd, 0, &t7, nullptr));
    before = std::chrono::high_resolution_clock::now();
    expect(read(fd, &counter, sizeof(counter)), (ssize_t)8);
    expect(counter, (u64)1);
    expect(read(fd, &counter, sizeof(counter)), (ssize_t)8);
    expect(counter, (u64)1);
    after = std::chrono::high_resolution_clock::now();
    expectge((int64_t)std::chrono::duration_cast<std::chrono::nanoseconds> (after-before).count(), (int64_t)600_ms);

    // Check timerfd_gettime():
    itimerspec t8 {{0,400_ms},{0,300_ms}};
    expect_success(junk, timerfd_settime(fd, 0, &t8, nullptr));
    itimerspec tout;
    // Right in the beginning:
    expect_success(junk, timerfd_gettime(fd, &tout));
    expect(tout.it_interval.tv_sec, t8.it_interval.tv_sec);
    expect(tout.it_interval.tv_nsec, t8.it_interval.tv_nsec);
    expect(tout.it_value.tv_sec, t8.it_value.tv_sec);
    expectge(t8.it_value.tv_nsec, tout.it_value.tv_nsec);
    expectge(tout.it_value.tv_nsec, t8.it_value.tv_nsec-(long)100_ms);
    // After a while but before expiration:
    usleep(100000);
    expect_success(junk, timerfd_gettime(fd, &tout));
    expect(tout.it_interval.tv_sec, t8.it_interval.tv_sec);
    expect(tout.it_interval.tv_nsec, t8.it_interval.tv_nsec);
    expect(tout.it_value.tv_sec, t8.it_value.tv_sec);
    expectge(t8.it_value.tv_nsec-(long)100_ms, tout.it_value.tv_nsec);
    expectge(tout.it_value.tv_nsec, t8.it_value.tv_nsec-(long)200_ms);
    // After expiration, we have the interval
    usleep(300000);
    expect_success(junk, timerfd_gettime(fd, &tout));
    expect(tout.it_interval.tv_sec, t8.it_interval.tv_sec);
    expect(tout.it_interval.tv_nsec, t8.it_interval.tv_nsec);
    expect(tout.it_value.tv_sec, t8.it_value.tv_sec);
    expectge(tout.it_value.tv_nsec, (long)200_ms);
    expectge((long)400_ms, tout.it_value.tv_nsec);

    // Check timerfd_gettime() after expiration of a single-time timer:
    itimerspec t9 {{0,0},{0,100_ms}};
    expect_success(junk, timerfd_settime(fd, 0, &t9, nullptr));
    usleep(200000);
    expect_success(junk, timerfd_gettime(fd, &tout));
    expect(tout.it_interval.tv_sec, (long)0);
    expect(tout.it_interval.tv_nsec, (long)0);
    expect(tout.it_value.tv_sec, (long)0);
    expect(tout.it_value.tv_nsec, (long)0);
    expect(tout.it_value.tv_nsec, (long)0);


}

int main(int argc, char **argv)
{
    dotest(CLOCK_REALTIME);
    dotest(CLOCK_MONOTONIC);
    std::cout << "SUMMARY: " << tests << " tests, " << fails << " failures\n";
    return fails == 0 ? 0 : 1;
}
