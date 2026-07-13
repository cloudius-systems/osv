/*
 * Copyright (C) 2017 ScyllaDB
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#define BOOST_TEST_MODULE tst-tcp-v6

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <vector>
#include <thread>
#include <chrono>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <osv/latch.hh>
#include <boost/test/unit_test.hpp>
#include <boost/interprocess/sync/interprocess_semaphore.hpp>

#ifdef __OSV__
#include <bsd/porting/netport.h> // Get INET6 
#else
#define INET6
#endif

#ifndef INET6

/*
 * This is a test for OSv's IPv6 non-support :-) Although we do not support
 * IPv6, we should return the errors which applications expect - see issue
 * #865 on how us returning the wrong error from socket() confused redis.
 */


BOOST_AUTO_TEST_CASE(test_no_ipv6)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    BOOST_REQUIRE_MESSAGE(sock>=0, "open AF_INET socket succeeds");
    close(sock);
    sock = socket(AF_INET6, SOCK_STREAM, 0);
    BOOST_REQUIRE_MESSAGE(sock<0, "open AF_INET6 socket fails");
    BOOST_REQUIRE_MESSAGE(errno==EAFNOSUPPORT, "failure should be EAFNOSUPPORT");
}

#else

/*
 * OSv compiled with IP6 support.
 */

BOOST_AUTO_TEST_CASE(test_has_ipv6)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    BOOST_REQUIRE_MESSAGE(sock>=0, "open AF_INET socket succeeds");
    close(sock);
    sock = socket(AF_INET6, SOCK_STREAM, 0);
    BOOST_REQUIRE_MESSAGE(sock>=0, "open AF_INET6 socket succeeds");
    close(sock);
}

#define LISTEN_PORT 7777

using _clock = std::chrono::high_resolution_clock;

static int accept_with_timeout(int listening_socket, int timeout_in_seconds)
{
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(listening_socket, &rfds);

    struct timeval tv = { .tv_sec = timeout_in_seconds, .tv_usec = 0 };
    int retval = select(listening_socket + 1, &rfds, NULL, NULL, &tv);
    BOOST_REQUIRE_EQUAL(1, retval);

    int client_socket = accept(listening_socket, NULL, NULL);
    BOOST_REQUIRE(client_socket > 0);
    return client_socket;
}

BOOST_AUTO_TEST_CASE(test_ipv6_connections_get_accepted_even_when_backlog_gets_overflowed)
{
    std::vector<int> sockets_to_close;

    constexpr int n_connections = 7;
    constexpr int backlog_size = 2;

    static_assert(n_connections < SOMAXCONN,
        "The number of connections should not exceed maximum backlog size");

    static_assert(backlog_size < n_connections,
        "The test makes sense only when number of connections is greater than backlog size");

    auto listen_s = socket(AF_INET6, SOCK_STREAM, 0);
    BOOST_REQUIRE(listen_s > 0);

    sockets_to_close.push_back(listen_s);

    int reuse = 1;
    BOOST_REQUIRE(setsockopt(listen_s, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) == 0);

    struct sockaddr_in6 laddr = {};
    laddr.sin6_family = AF_INET6;
    laddr.sin6_addr = in6addr_any;
    laddr.sin6_port = htons(LISTEN_PORT);

    BOOST_REQUIRE(bind(listen_s, (struct sockaddr *) &laddr, sizeof(laddr)) == 0);
    BOOST_REQUIRE(listen(listen_s, backlog_size) == 0);

    BOOST_TEST_MESSAGE("listening...");

    for (int i = 0; i < n_connections; i++) {
        int s = socket(AF_INET6, SOCK_STREAM, 0);
        BOOST_REQUIRE(s > 0);

        struct sockaddr_in6 raddr = {};
        raddr.sin6_family = AF_INET6;
        inet_pton(AF_INET6, "::1", &raddr.sin6_addr);
        raddr.sin6_port = htons(LISTEN_PORT);

        BOOST_TEST_MESSAGE("connecting...");

        BOOST_REQUIRE(connect(s, (struct sockaddr *)&raddr, sizeof(raddr)) == 0);
        sockets_to_close.push_back(s);
    }

    BOOST_TEST_MESSAGE("starting to accept...");

    for (int i = 0; i < n_connections; i++) {
        int client_s = accept_with_timeout(listen_s, 3);
        BOOST_REQUIRE(client_s >= 0);
        BOOST_TEST_MESSAGE("accepted");

        sockets_to_close.push_back(client_s);
    }

    BOOST_TEST_MESSAGE("closing...");

    for (auto& fd : sockets_to_close) {
        close(fd);
    }
}

BOOST_AUTO_TEST_CASE(test_ipv6_clients_are_not_reset_when_backlog_is_full_and_they_write)
{
    constexpr int backlog_size = 5;
    constexpr int n_connections = backlog_size * 3;

    auto listen_s = socket(AF_INET6, SOCK_STREAM, 0);
    if (listen_s < 0) {
        perror("socket");
        BOOST_REQUIRE(false);
        exit(1);
    }

    int reuse = 1;
    BOOST_REQUIRE(setsockopt(listen_s, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) == 0);

    struct sockaddr_in6 laddr = {};
    laddr.sin6_family = AF_INET6;
    laddr.sin6_addr = in6addr_any;
    laddr.sin6_port = htons(LISTEN_PORT);

    if (bind(listen_s, (struct sockaddr *) &laddr, sizeof(laddr)) < 0) {
        perror("bind");
        BOOST_REQUIRE(false);
        exit(1);
    }

    BOOST_REQUIRE(listen(listen_s, backlog_size) == 0);

    BOOST_TEST_MESSAGE("listening...");

    std::vector<std::thread*> threads;

    latch _latch(n_connections);

    for (int i = 0; i < n_connections; i++) {
        threads.push_back(new std::thread([i, &_latch] {
            int s = socket(AF_INET6, SOCK_STREAM, 0);
            if (s < 0) {
                perror("socket");
                BOOST_REQUIRE(false);
                exit(1);
            }

            struct sockaddr_in6 raddr = {};
            raddr.sin6_family = AF_INET6;
            inet_pton(AF_INET6, "::1", &raddr.sin6_addr);
            raddr.sin6_port = htons(LISTEN_PORT);

            _latch.count_down();

            if (connect(s, (struct sockaddr *)&raddr, sizeof(raddr))) {
                perror("connect");
                BOOST_REQUIRE(false);
                exit(1);
            }

            while (true) {
                const char* msg = "hello";
                int bytes = write(s, msg, strlen(msg) + 1);
                if (bytes < 0) {
                    break;
                }
            }
            close(s);
        }));
    }

    // Start accepting after all clients are lined up
    // to create a thundering storm effect.
    _latch.await();

    for (int i = 0; i < n_connections; i++) {
        int client_s = accept_with_timeout(listen_s, 3);
        BOOST_TEST_MESSAGE("accepted");

        threads.push_back(new std::thread([client_s] {
            auto close_at = _clock::now() + std::chrono::milliseconds(50);
            while (_clock::now() < close_at) {
                char buf[1];
                int bytes = read(client_s, &buf, sizeof(buf));
                if (bytes < 0) {
                    perror("read");
                    BOOST_REQUIRE(false);
                    exit(1);
                }
            }

            close(client_s);
        }));
    }

    for (auto& thread : threads) {
        thread->join();
        delete thread;
    }

    close(listen_s);
}

#endif /* INET6 */

