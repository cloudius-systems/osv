/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#define BOOST_TEST_MODULE tst-bsd-tcp1

#include <boost/test/unit_test.hpp>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstdio>
#include <thread>

#include "tst-hub.hh"

#include <boost/asio.hpp>

#if CONF_logger_debug
    #define dbg_d(tag, ...) printf(__VA_ARGS__)
#else
    #define dbg_d(tag, ...) do{}while(0)
#endif

#define LISTEN_PORT (5555)
#define ITERATIONS (400)
const int buf_size = 512;

class test_bsd_tcp1 {
public:
    test_bsd_tcp1() {}
    virtual ~test_bsd_tcp1() {}

    /* Simple server implementation */
    int tcp_server(void)
    {
        int listen_s;
        int client_s;
        int optval, error;
        struct sockaddr_in laddr;
        char buf[buf_size] = {};
        int i;

        /* Create listening socket */
        dbg_d("tst-tcp1: server: creating listening socket...");
        listen_s = socket(AF_INET, SOCK_STREAM, 0);
        if (listen_s < 0) {
            dbg_d("tst-tcp1: server: socket() failed!");
            return -1;
        }

        /* Reuse bind address */
        optval = 1;
        error = setsockopt(listen_s, SOL_SOCKET, SO_REUSEADDR, &optval,
            sizeof(optval));
        if (error < 0) {
            dbg_d("tst-tcp1: server: setsockopt(SO_REUSEADDR) failed, error=%d", error);
            return -1;
        }

        /* Bind */
        memset(&laddr, 0, sizeof(laddr));
        laddr.sin_family = AF_INET;
        // laddr.sin_addr.s_addr = htonl(INADDR_ANY);
        inet_aton("127.0.0.1", &laddr.sin_addr);
        laddr.sin_port = htons(LISTEN_PORT);

        dbg_d("tst-tcp1: server: calling bind()");
        if ( bind(listen_s, (struct sockaddr *) &laddr, sizeof(laddr)) < 0 ) {
            dbg_d("tst-tcp1: server: bind() failed!");
            return -1;
        }

        /* Listen */
        dbg_d("tst-tcp1: server: calling listen()");
        if ( listen(listen_s, 256) < 0 ) {
            dbg_d("tst-tcp1: server: listen() failed!");
            return -1;
        }

        for (i=0; i < ITERATIONS; i++) {

            /*  Wait for a connection*/
            dbg_d("tst-tcp1: server: calling accept()");
            if ( (client_s = accept(listen_s, NULL, NULL) ) < 0 ) {
                dbg_d("tst-tcp1: server: accept() failed!");
                return -1;
            }

            dbg_d("tst-tcp1: server: got a connection!");

            /* Read max buf_size bytes */
            int bytes = read(client_s, &buf, buf_size);
            if (bytes < 0) {
                dbg_d("tst-tcp1: server: read() failed!");
                close(client_s);
                return -1;
            }

            /* Echo back */
            dbg_d("tst-tcp1: Echoing %d bytes", bytes);

            int bytes2 = write(client_s, &buf, bytes);
            if (bytes2 < 0) {
                dbg_d("tst-tcp1: server: write() failed!");
                close(client_s);
                return -1;
            }

            dbg_d("tst-tcp1: server: echoed %d bytes", bytes2);
            dbg_d("tst-tcp1: server: calling close()", bytes2);

            if ( close(client_s) < 0 ) {
                dbg_d("tst-tcp1: server: close() failed!");
                return -1;
            }
        }

        if ( close(listen_s) < 0 ) {
            dbg_d("tst-tcp1: server: close() failed!");
            return -1;
        }

        dbg_d("tst-tcp1: server: DONE");
        return 0;
    }

    int tcp_client(void)
    {
        struct sockaddr_in raddr;
        const char * message = "This is a TCP message";
        char replay[buf_size];

        memset(&raddr, 0, sizeof(raddr));
        raddr.sin_family = AF_INET;
        inet_aton("127.0.0.1", &raddr.sin_addr);
        raddr.sin_port = htons(LISTEN_PORT);

        for (int i=0; i < ITERATIONS; i++) {

            dbg_d("tst-tcp1: client: creating socket()... #%d", i+1);
            int s = socket(AF_INET, SOCK_STREAM, 0);
            if (s < 0) {
                dbg_d("tst-tcp1: client: socket() failed!");
                return -1;
            }

            dbg_d("tst-tcp1: client: connecting to 127.0.0.1...");
            if ( connect(s, (struct sockaddr *)&raddr, sizeof(raddr)) < 0 ) {
                dbg_d("tst-tcp1: client: connect() failed!");
                return -1;
            }

            dbg_d("tst-tcp1: client: writing message: %s", message);
            int bytes = write(s, message, strlen(message) + 1);
            if (bytes < 0) {
                dbg_d("tst-tcp1: client: write() failed!");
                return -1;
            }

            int bytes2 = read(s, &replay, buf_size);
            if (bytes2 < 0) {
                 dbg_d("tst-tcp1: client: read() failed!");
                 return -1;
             }

            replay[bytes2] = '\0';
            dbg_d("tst-tcp1: client: got replay: %s", replay);

            dbg_d("tst-tcp1: client: closing socket()");
            if ( close(s) < 0 ) {
                dbg_d("tst-tcp1: client: close() failed!");
                return -1;
            }
        }

        dbg_d("tst-tcp1: client: DONE");
        return 0;
    }

    int run(void)
    {
        _client_result = 0;
        _server_result = 0;

        std::thread srv([&] {
            _server_result = tcp_server();
        });
        sleep(1);
        std::thread cli([&] {
            _client_result = tcp_client();
        });

        cli.join();
        srv.join();

        return (_client_result + _server_result);
    }

private:
    int _client_result;
    int _server_result;
};

BOOST_AUTO_TEST_CASE(test_tcp_client_server)
{
    dbg_d("tst-tcp1: BSD TCP1 Test - Begin");

    test_bsd_tcp1 tcp1;
    int rc = tcp1.run();

    BOOST_REQUIRE(rc >= 0);

    dbg_d("tst-tcp1: BSD TCP1 Test completed: %s!", rc >= 0 ? "PASS" : "FAIL");
    dbg_d("tst-tcp1: BSD TCP1 Test - End");
}


BOOST_AUTO_TEST_CASE(test_shutdown_wr)
{
    using namespace boost::asio::ip;
    boost::asio::io_service io_service;
    tcp::acceptor acceptor(io_service, tcp::endpoint(tcp::v4(), 10000));
    tcp::socket client(io_service);
    client.connect(tcp::endpoint(address_v4::from_string("127.0.0.1"), 10000));
    tcp::socket server(io_service);
    acceptor.accept(server);
    server.shutdown(tcp::socket::shutdown_send);
}

#undef ITERATIONS
#undef LISTEN_PORT
