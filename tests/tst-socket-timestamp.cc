/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
// To compile on Linux, use: g++ -g -pthread -std=c++11 tests/tst-uio.cc

// This test tests the SO_TIMESTMAP socket option.

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/uio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>

#include <iostream>
#include <thread>
#include <vector>
#include <mutex>

// Multiple threads can call expect functions at the same time
// so need to protect against concurrent writes to cout.
std::mutex test_mutex; 

static int tests = 0, fails = 0;

template<typename T>
bool do_expect(T actual, T expected, const char *actuals, const char *expecteds, const char *file, int line)
{
    std::lock_guard<std::mutex> lock(test_mutex);

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
    std::lock_guard<std::mutex> lock(test_mutex);

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

int main()
{
    int sockfd;
    int optval = 1;
    struct sockaddr_in serveraddr;
    socklen_t serveraddr_len = sizeof(serveraddr);
    const int npacket = 5;
    int ret;

    expect_success(sockfd, socket(AF_INET, SOCK_DGRAM, 0));
    expect(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)), 0);
    expect(setsockopt(sockfd, SOL_SOCKET, SO_TIMESTAMP, &optval, sizeof(optval)), 0);

    bzero(&serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    serveraddr.sin_port = 0; // Find next available port
    expect_success(ret, bind(sockfd, (struct sockaddr*) &serveraddr, serveraddr_len));

    expect_success(ret, getsockname(sockfd, (struct sockaddr*) &serveraddr, &serveraddr_len));
    expect(serveraddr.sin_family, (in_port_t)AF_INET);
    std::cout << "Server bound to UDP port " << ntohs(serveraddr.sin_port) << std::endl;

    std::thread t([sockfd, npacket] {
        struct msghdr msg;
        struct iovec iov;
        uint8_t buf[64];
        uint8_t controlbuf[CMSG_SPACE(sizeof(struct timeval))];
        struct timeval start, end;
        std::vector<struct timeval> timestamps;

        gettimeofday(&start, NULL);

        bzero(&msg, sizeof(msg));
        bzero(&iov, sizeof(iov));
        for (int ipacket = 0; ipacket < npacket; ++ipacket) {
            iov.iov_base = buf;
            iov.iov_len = sizeof(buf);
            msg.msg_iov = &iov;
            msg.msg_iovlen = 1;
            msg.msg_control = controlbuf;
            msg.msg_controllen = sizeof(controlbuf);

            int n;
            expect_success(n, recvmsg(sockfd, &msg, 0));
            expect(n, 6);

            // Extract receive timestamp from cmsg data
            for (auto cmptr = CMSG_FIRSTHDR(&msg); cmptr != NULL; cmptr = CMSG_NXTHDR(&msg, cmptr)) {
                if ((cmptr->cmsg_level == SOL_SOCKET) && (cmptr->cmsg_type == SCM_TIMESTAMP)) {
                    struct timeval tv;
                    memcpy(&tv, CMSG_DATA(cmptr), sizeof(tv));
                    timestamps.push_back(tv);
                }
            }
        }

        gettimeofday(&end, NULL);

        // Validate received timestamps
        expect(timestamps.size(), (std::size_t)5);
        const struct timeval *prev_tv = NULL;
        for (auto const & tv : timestamps) {
            expect(timercmp(&tv, &start, >=), true);
            expect(timercmp(&tv, &end, <=), true);
            if (prev_tv) {
               expect(timercmp(&tv, prev_tv, >=), true);
            }
            prev_tv = &tv;
        }
    });

    int sendsock;

    expect_success(sendsock, socket(AF_INET, SOCK_DGRAM, 0));
    for (int ipacket = 0; ipacket < npacket; ++ipacket) {
        expect_success(ret, sendto(sendsock, "Hello!", 6, 0, (const sockaddr*) &serveraddr, sizeof(serveraddr)));
    }
    t.join();
    close(sockfd);
    close(sendsock);

    std::cout << "SUMMARY: " << tests << " tests, " << fails << " failures\n";
    return fails == 0 ? 0 : 1;
}

