#include <sys/socket.h>
#include <sys/poll.h>
#include <unistd.h>
#include "sched.hh"
#include "debug.hh"

int tests = 0, fails = 0;

static void report(bool ok, const char* msg)
{
    ++tests;
    fails += !ok;
    debug("%s: %s\n", (ok ? "PASS" : "FAIL"), msg);
}

int main(int ac, char** av)
{
    int s[2];

    int r = pipe(s);
    report(r == 0, "pipe call");


    char msg[] = "hello", reply[] = "wrong";
    r = write(s[1], msg, 5);
    report(r == 5, "write to empty socket");
    r = read(s[0], reply, 5);
    report(r == 5 && memcmp(msg, reply, 5) == 0, "read after write");

    r = write(s[0], msg, 5);
    report(r == -1 && errno == EBADF, "write to read end should fail");
    r = read(s[1], reply, 5);
    report(r == -1 && errno == EBADF, "read to write end should fail");

    memcpy(msg, "snafu", 5);
    memset(reply, 0, 5);
    int r2;
    sched::thread t1([&] {
        r2 = read(s[0], reply, 5);
    });
    t1.start();
    sleep(1);
    r = write(s[1], msg, 5);
    t1.join();
    report(r2 == 5 && memcmp(msg, reply, 5) == 0, "read before write");


    memcpy(msg, "fooba", 5);
    memset(reply, 0, 5);
    pollfd poller = { s[0], POLLIN, 0 };
    r = poll(&poller, 1, 0);
    report(r == 0, "poll() (immediate, no events)");
    r = poll(&poller, 1, 1);
    report(r == 0, "poll() (timeout, no events)");
    r2 = write(s[1], msg, 5);
    r = poll(&poller, 1, 0);
    report(r2 == 5 && r == 1 && poller.revents == POLLIN, "poll() (immediate, has event)");
    r2 = read(s[0], reply, 5);
    report(r2 == 5 && memcmp(msg, reply, 5) == 0, "read after poll");


    memcpy(msg, "smeg!", 5);
    memset(reply, 0, 5);
    sched::thread t2([&] {
        poller.revents = 0;
        r2 = poll(&poller, 1, 5000);
        report(r2 == 1 && poller.revents == POLLIN, "waiting poll");
        r2 = read(s[0], reply, 5);
        report(r2 == 5 && memcmp(msg, reply, 5) == 0, "read after waiting poll");
    });
    t2.start();
    sleep(1);
    r = write(s[1], msg, 5);
    t2.join();
    report(r == 5, "write to polling socket");

    poller = { s[0], POLLOUT, 0 };
    r = poll(&poller, 1, 0);
    report(r == 0, "poll() (no output on read end)");
    poller = { s[1], POLLIN, 0 };
    r = poll(&poller, 1, 0);
    report(r == 0, "poll() (no input on write end)");

    r = close(s[0]);
    report(r == 0, "close read side first");
    r = write(s[1], msg, 5);
    report(r == -1 && errno == EPIPE, "write, other side closed");
    r = close(s[1]);
    report(r == 0, "close also write side");


    r = pipe(s);
    report(r == 0, "pipe call");
    r = close(s[1]);
    report(r == 0, "close write side first");
    r = read(s[0], msg, 5);
    report(r == 0, "read, other side closed");
    r = close(s[0]);
    report(r == 0, "close also read side");


    std::vector<int> fds;
    while (pipe(s) == 0) {
        fds.push_back(s[0]);
        fds.push_back(s[1]);
    }
    auto n = fds.size();
    for (int fd : fds) {
        close(fd);
    }
    debug("n=%d\n",n);
    report(n > 100, "create many pipes");

    debug("SUMMARY: %d tests, %d failures\n", tests, fails);
}


