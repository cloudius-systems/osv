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

#ifdef __OSV__
#include <bsd/porting/netport.h> // Get INET6
#else
#define INET6
#endif


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

void test_ipv4()
{
    int sockfd;
    int optval = 1;
    struct sockaddr_in serveraddr;
    socklen_t serveraddr_len = sizeof(serveraddr);
    const int npacket = 5;
    int ret;

    expect_success(sockfd, socket(AF_INET, SOCK_DGRAM, 0));
    expect_success(ret, setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)));
    expect_success(ret, setsockopt(sockfd, IPPROTO_IP, IP_PKTINFO, &optval, sizeof(optval)));

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
        uint8_t controlbuf[CMSG_SPACE(sizeof(struct in_pktinfo))];

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

            struct in_pktinfo pktinfo;
            bool pktinfo_valid = false;

            for (auto cmptr = CMSG_FIRSTHDR(&msg); cmptr != NULL; cmptr = CMSG_NXTHDR(&msg, cmptr)) {
                if ((cmptr->cmsg_level == IPPROTO_IP) && (cmptr->cmsg_type == IP_PKTINFO)) {
                    memcpy(&pktinfo, CMSG_DATA(cmptr), sizeof(pktinfo));
                    pktinfo_valid = true;
                    break;
                }
            }

            expect(pktinfo_valid, true);

            char ipaddr[INET_ADDRSTRLEN]; 
            inet_ntop(AF_INET, &pktinfo.ipi_addr, ipaddr, sizeof(ipaddr));
            std::cout << "ifindex " << pktinfo.ipi_ifindex << " ipaddr " << ipaddr << std::endl;
            expect(pktinfo.ipi_addr.s_addr, htonl(INADDR_LOOPBACK));
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
}

#ifdef INET6

void test_ipv6()
{
    int sockfd;
    int optval = 1;
    struct sockaddr_in6 serveraddr;
    socklen_t serveraddr_len = sizeof(serveraddr);
    const int npacket = 5;
    int ret;

    expect_success(sockfd, socket(AF_INET6, SOCK_DGRAM, 0));
    expect_success(ret, setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)));
    expect_success(ret, setsockopt(sockfd, IPPROTO_IPV6, IPV6_RECVPKTINFO, &optval, sizeof(optval)));

    bzero(&serveraddr, sizeof(serveraddr));
    serveraddr.sin6_family = AF_INET6;
    serveraddr.sin6_addr = in6addr_loopback;
    serveraddr.sin6_port = 0; // Find next available port
    expect_success(ret, bind(sockfd, (struct sockaddr*) &serveraddr, serveraddr_len));

    expect_success(ret, getsockname(sockfd, (struct sockaddr*) &serveraddr, &serveraddr_len));
    expect(serveraddr.sin6_family, (in_port_t)AF_INET6);
    std::cout << "Server bound to UDP port " << ntohs(serveraddr.sin6_port) << std::endl;

    std::thread t([sockfd, npacket] {
        struct msghdr msg;
        struct iovec iov;
        uint8_t buf[64];
        uint8_t controlbuf[CMSG_SPACE(sizeof(struct in6_pktinfo))];

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

            struct in6_pktinfo pktinfo;
            bool pktinfo_valid = false;

            for (auto cmptr = CMSG_FIRSTHDR(&msg); cmptr != NULL; cmptr = CMSG_NXTHDR(&msg, cmptr)) {
                if ((cmptr->cmsg_level == IPPROTO_IPV6) && (cmptr->cmsg_type == IPV6_PKTINFO)) {
                    memcpy(&pktinfo, CMSG_DATA(cmptr), sizeof(pktinfo));
                    pktinfo_valid = true;
                    break;
                }
            }

            expect(pktinfo_valid, true);

            char ipaddr[INET6_ADDRSTRLEN]; 
            inet_ntop(AF_INET6, &pktinfo.ipi6_addr, ipaddr, sizeof(ipaddr));
            std::cout << "ifindex " << pktinfo.ipi6_ifindex << " ipaddr " << ipaddr << std::endl;
            expect(std::string(ipaddr), std::string("::1"));
        }
    });

    int sendsock;

    expect_success(sendsock, socket(AF_INET6, SOCK_DGRAM, 0));
    for (int ipacket = 0; ipacket < npacket; ++ipacket) {
        expect_success(ret, sendto(sendsock, "Hello!", 6, 0, (const sockaddr*) &serveraddr, sizeof(serveraddr)));
    }
    t.join();
    close(sockfd);
    close(sendsock);
}

#endif

int main()
{
    test_ipv4();
#ifdef INET6
    test_ipv6();
#endif
    std::cout << "SUMMARY: " << tests << " tests, " << fails << " failures\n";
    return fails == 0 ? 0 : 1;
}

