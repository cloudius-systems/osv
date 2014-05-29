/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * To compile on Linux:
 * g++ -g -pthread -std=c++11 tests/tst-poll.cc -o tests/tst-poll \
 *   -I./include -lboost_unit_test_framework -DBOOST_TEST_DYN_LINK
 */

#define BOOST_TEST_MODULE tst-poll

#include <thread>
#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <osv/latch.hh>
#include <boost/test/unit_test.hpp>

#define LISTEN_PORT 7777

BOOST_AUTO_TEST_CASE(test_polling_on_negative_fd_yields_no_events)
{
    struct pollfd pfd_array[2];
    for (auto& pfd : pfd_array) {
        pfd.fd = -1;
        pfd.events = POLLIN;
    }

    BOOST_MESSAGE("test 1 file case");
    pfd_array[0].revents = POLLIN;
    BOOST_REQUIRE(poll(pfd_array, 1, 0) == 0);
    BOOST_REQUIRE(pfd_array[0].revents == 0);

    /* See issue #323

    BOOST_MESSAGE("test many files case");
    pfd_array[0].revents = POLLIN;
    pfd_array[1].revents = POLLIN;
    BOOST_REQUIRE(poll(pfd_array, 2, 0) == 0);
    BOOST_REQUIRE(pfd_array[0].revents == 0);
    BOOST_REQUIRE(pfd_array[1].revents == 0);

    */
}

BOOST_AUTO_TEST_CASE(test_polling_on_invalid_fd_yields_pollnval_event)
{
    struct pollfd pfd_array[2];
    for (auto& pfd : pfd_array) {
        pfd.fd = 100;
        pfd.events = POLLIN;
    }

    BOOST_MESSAGE("test 1 file case");
    pfd_array[0].revents = 0;
    BOOST_REQUIRE(poll(pfd_array, 1, 0) == 1);
    BOOST_REQUIRE(pfd_array[0].revents == POLLNVAL);

    BOOST_MESSAGE("test many files case");
    pfd_array[0].revents = 0;
    pfd_array[1].revents = 0;
    BOOST_REQUIRE(poll(pfd_array, 2, 0) == 2);
    BOOST_REQUIRE(pfd_array[0].revents == POLLNVAL);
    BOOST_REQUIRE(pfd_array[1].revents == POLLNVAL);
}

BOOST_AUTO_TEST_CASE(test_polling_on_one_socket)
{
    auto s = socket(AF_INET, SOCK_STREAM, 0);
    BOOST_REQUIRE(s > 0);

    struct sockaddr_in laddr = {};
    laddr.sin_family = AF_INET;
    laddr.sin_addr.s_addr = htonl(INADDR_ANY);
    laddr.sin_port = htons(LISTEN_PORT);

    BOOST_REQUIRE(bind(s, (struct sockaddr *) &laddr, sizeof(laddr)) == 0);
    BOOST_REQUIRE(listen(s, SOMAXCONN) == 0);

    latch can_write;
    latch can_read;
    latch can_close;

    auto t = std::thread([&] {
        int s = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
        BOOST_REQUIRE(s > 0);

        struct sockaddr_in raddr = {};
        raddr.sin_family = AF_INET;
        inet_aton("127.0.0.1", &raddr.sin_addr);
        raddr.sin_port = htons(LISTEN_PORT);

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        BOOST_MESSAGE("connecting...");
        auto ret = connect(s, (struct sockaddr *)&raddr, sizeof(raddr));
        BOOST_REQUIRE(ret == 0 || errno == EINPROGRESS);

        struct pollfd pfd;
        pfd.fd = s;
        pfd.events = POLLOUT;
        BOOST_REQUIRE(poll(&pfd, 1, -1) == 1);

        BOOST_MESSAGE("conected...");

        can_write.await();

        BOOST_MESSAGE("writing...");
        BOOST_REQUIRE(write(s, "!", 1) == 1);

        can_read.await();

        BOOST_MESSAGE("reading...");

        char buf[16];
        do {} while (read(s, buf, sizeof(buf)) > 0);

        BOOST_MESSAGE("done...");

        can_close.await();

        BOOST_MESSAGE("closing...");
        close(s);
    });

    BOOST_MESSAGE("checking that poll will return POLLIN when accept() is ready");
    struct pollfd pfd;
    pfd.fd = s;
    pfd.events = POLLIN;
    BOOST_REQUIRE(poll(&pfd, 1, 500) == 1);
    BOOST_REQUIRE(pfd.revents & POLLIN);

    int client_socket = accept4(s, NULL, NULL, SOCK_NONBLOCK);
    BOOST_REQUIRE(client_socket > 0);

    pfd.fd = client_socket;
    pfd.events = POLLOUT;
    BOOST_REQUIRE(poll(&pfd, 1, 500) == 1);

    BOOST_MESSAGE("checking that poll will not return POLLIN when nothing to read");
    pfd.events = POLLIN;
    BOOST_REQUIRE(poll(&pfd, 1, 0) == 0);
    BOOST_REQUIRE(pfd.revents == 0);

    can_write.count_down();

    BOOST_MESSAGE("checking that poll will return POLLIN when read() is ready");
    pfd.events = POLLIN;
    BOOST_REQUIRE(poll(&pfd, 1, 500) == 1);
    BOOST_REQUIRE(pfd.revents == POLLIN);

    BOOST_MESSAGE("checking that poll will return both POLLIN and POLLOUT");
    pfd.events = POLLIN | POLLOUT;
    BOOST_REQUIRE(poll(&pfd, 1, 0) == 1);
    BOOST_REQUIRE(pfd.revents == (POLLIN | POLLOUT));

    char buf[16];
    BOOST_REQUIRE(read(client_socket, buf, 1) == 1);

    BOOST_MESSAGE("checking no events after read");
    pfd.events = POLLIN;
    BOOST_REQUIRE(poll(&pfd, 1, 0) == 0);
    BOOST_REQUIRE(pfd.revents == 0);

    BOOST_MESSAGE("filling up socket buffer...");
    do {} while (write(client_socket, buf, sizeof(buf)) > 0);

    can_read.count_down();

    BOOST_MESSAGE("checking socket is unblocked for write");
    pfd.events = POLLOUT;
    BOOST_REQUIRE(poll(&pfd, 1, 500) == 1);
    BOOST_REQUIRE(pfd.revents == POLLOUT);

    can_close.count_down();

    BOOST_MESSAGE("checking that poll will return POLLIN when close() is ready");
    pfd.events = POLLIN;
    BOOST_REQUIRE(poll(&pfd, 1, 500) == 1);
    BOOST_REQUIRE(pfd.revents == POLLIN);

    close(client_socket);

    t.join();
}
