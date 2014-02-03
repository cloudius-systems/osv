/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#define BOOST_TEST_MODULE tst-bsd-tcp1

#include <boost/test/unit_test.hpp>

#include <osv/sched.hh>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <osv/debug.hh>
#include "tst-hub.hh"

#define dbg_d(...) tprintf_d("tst-tcp1", __VA_ARGS__)

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
        dbg_d("server: creating listening socket...");
        listen_s = socket(AF_INET, SOCK_STREAM, 0);
        if (listen_s < 0) {
            dbg_d("server: socket() failed!");
            return -1;
        }

        /* Reuse bind address */
        optval = 1;
        error = setsockopt(listen_s, SOL_SOCKET, SO_REUSEADDR, &optval,
            sizeof(optval));
        if (error < 0) {
            dbg_d("server: setsockopt(SO_REUSEADDR) failed, error=%d", error);
            return -1;
        }

        /* Bind */
        memset(&laddr, 0, sizeof(laddr));
        laddr.sin_family = AF_INET;
        // laddr.sin_addr.s_addr = htonl(INADDR_ANY);
        inet_aton("127.0.0.1", &laddr.sin_addr);
        laddr.sin_port = htons(LISTEN_PORT);

        dbg_d("server: calling bind()");
        if ( bind(listen_s, (struct sockaddr *) &laddr, sizeof(laddr)) < 0 ) {
            dbg_d("server: bind() failed!");
            return -1;
        }

        /* Listen */
        dbg_d("server: calling listen()");
        if ( listen(listen_s, 256) < 0 ) {
            dbg_d("server: listen() failed!");
            return -1;
        }

        for (i=0; i < ITERATIONS; i++) {

            /*  Wait for a connection*/
            dbg_d("server: calling accept()");
            if ( (client_s = accept(listen_s, NULL, NULL) ) < 0 ) {
                dbg_d("server: accept() failed!");
                return -1;
            }

            dbg_d("server: got a connection!");

            /* Read max buf_size bytes */
            int bytes = read(client_s, &buf, buf_size);
            if (bytes < 0) {
                dbg_d("server: read() failed!");
                close(client_s);
                return -1;
            }

            /* Echo back */
            dbg_d("Echoing %d bytes", bytes);

            int bytes2 = write(client_s, &buf, bytes);
            if (bytes2 < 0) {
                dbg_d("server: write() failed!");
                close(client_s);
                return -1;
            }

            dbg_d("server: echoed %d bytes", bytes2);
            dbg_d("server: calling close()", bytes2);

            if ( close(client_s) < 0 ) {
                dbg_d("server: close() failed!");
                return -1;
            }
        }

        if ( close(listen_s) < 0 ) {
            dbg_d("server: close() failed!");
            return -1;
        }

        dbg_d("server: DONE");
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

            dbg_d("client: creating socket()... #%d", i+1);
            int s = socket(AF_INET, SOCK_STREAM, 0);
            if (s < 0) {
                dbg_d("client: socket() failed!");
                return -1;
            }

            dbg_d("client: connecting to 127.0.0.1...");
            if ( connect(s, (struct sockaddr *)&raddr, sizeof(raddr)) < 0 ) {
                dbg_d("client: connect() failed!");
                return -1;
            }

            dbg_d("client: writing message: %s", message);
            int bytes = write(s, message, strlen(message) + 1);
            if (bytes < 0) {
                dbg_d("client: write() failed!");
                return -1;
            }

            int bytes2 = read(s, &replay, buf_size);
            if (bytes2 < 0) {
                 dbg_d("client: read() failed!");
                 return -1;
             }

            replay[bytes2] = '\0';
            dbg_d("client: got replay: %s", replay);

            dbg_d("client: closing socket()");
            if ( close(s) < 0 ) {
                dbg_d("client: close() failed!");
                return -1;
            }
        }

        dbg_d("client: DONE");
        return 0;
    }

    int run(void)
    {
        _client_result = 0;
        _server_result = 0;

        sched::thread *srv = new sched::thread([&] {
            _server_result = tcp_server();
        });
        sched::thread *cli = new sched::thread([&] {
            _client_result = tcp_client();
        });

        srv->start();
        sleep(1);
        cli->start();

        cli->join();
        srv->join();
        delete(cli);
        delete(srv);

        return (_client_result + _server_result);
    }

private:
    int _client_result;
    int _server_result;
};

BOOST_AUTO_TEST_CASE(test_tcp_client_server)
{
    dbg_d("BSD TCP1 Test - Begin");

    test_bsd_tcp1 tcp1;
    int rc = tcp1.run();

    BOOST_REQUIRE(rc >= 0);

    dbg_d("BSD TCP1 Test completed: %s!", rc >= 0 ? "PASS" : "FAIL");
    dbg_d("BSD TCP1 Test - End");
}

#undef ITERATIONS
#undef LISTEN_PORT
#undef dbg_d
