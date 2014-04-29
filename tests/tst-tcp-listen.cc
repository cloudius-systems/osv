/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#define BOOST_TEST_MODULE tst-tcp-listen

#include <vector>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <boost/test/unit_test.hpp>

#define LISTEN_PORT 7777

BOOST_AUTO_TEST_CASE(test_connections_get_accepted_even_when_backlog_gets_overflowed)
{
    constexpr int n_connections = 7;
    constexpr int backlog_size = 2;

    static_assert(n_connections < SOMAXCONN,
        "The number of connections should not exceed maximum backlog size");

    static_assert(backlog_size < n_connections,
        "The test makes sense only when number of connections is greater than backlog size");

    auto listen_s = socket(AF_INET, SOCK_STREAM, 0);
    BOOST_REQUIRE(listen_s > 0);

    struct sockaddr_in laddr = {};
    laddr.sin_family = AF_INET;
    laddr.sin_addr.s_addr = htonl(INADDR_ANY);
    laddr.sin_port = htons(LISTEN_PORT);

    BOOST_REQUIRE(bind(listen_s, (struct sockaddr *) &laddr, sizeof(laddr)) == 0);
    BOOST_REQUIRE(listen(listen_s, backlog_size) == 0);

    BOOST_MESSAGE("listening...");

    std::vector<int> sockets_to_close;

    for (int i = 0; i < n_connections; i++) {
        int s = socket(AF_INET, SOCK_STREAM, 0);
        BOOST_REQUIRE(s > 0);

        struct sockaddr_in raddr = {};
        raddr.sin_family = AF_INET;
        inet_aton("127.0.0.1", &raddr.sin_addr);
        raddr.sin_port = htons(LISTEN_PORT);

        BOOST_MESSAGE("connecting...");

        BOOST_REQUIRE(connect(s, (struct sockaddr *)&raddr, sizeof(raddr)) == 0);
        sockets_to_close.push_back(s);
    }

    BOOST_MESSAGE("starting to accept...");

    for (int i = 0; i < n_connections; i++) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(listen_s, &rfds);

        // Wait 3 seconds before failing the test
        struct timeval tv = { .tv_sec = 3, .tv_usec = 0 };
        int retval = select(listen_s + 1, &rfds, NULL, NULL, &tv);
        BOOST_REQUIRE_EQUAL(1, retval);

        int client_s = accept(listen_s, NULL, NULL);
        BOOST_REQUIRE(client_s >= 0);
        BOOST_MESSAGE("accepted");

        sockets_to_close.push_back(client_s);
    }

    BOOST_MESSAGE("closing...");

    for (auto& fd : sockets_to_close) {
        close(fd);
    }
}
