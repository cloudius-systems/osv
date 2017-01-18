/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#define BOOST_TEST_MODULE tst-bsd-tcp1-zsndrcv

#include <boost/test/unit_test.hpp>

#include <osv/sched.hh>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <osv/debug.hh>
#include "tst-hub.hh"

#include <boost/asio.hpp>

#include <osv/zcopy.h>

#define dbg_d(...) tprintf_d("tst-tcp1-zsndrcv", __VA_ARGS__)

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
        int i;
        struct zmsghdr zm;
        struct iovec iov[1];
        struct pollfd pfd[1];

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

            pfd[0].fd = client_s;
            pfd[0].events = POLLIN;
            if (poll(pfd, 1, -1) < 0) {
                dbg_d("server: poll() failed!");
                close(client_s);
                return -1;
            }

            zm = {};
            zm.zm_msg.msg_iov = iov;
            zm.zm_msg.msg_iovlen = 32;

            /* Read max buf_size bytes */
            int bytes = zcopy_rx(client_s, &zm);
            if (bytes < 0) {
                dbg_d("server: read() failed!");
                close(client_s);
                return -1;
            }

            /* Echo back */
            dbg_d("Echoing %d bytes", bytes);

            int bytes2 = zcopy_tx(client_s, &zm);
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

            pfd[0].fd = zm.zm_txfd;
            pfd[0].events = POLLIN;
            if (poll(pfd, 1, -1) < 0) {
                dbg_d("server: poll() failed!");
                close(client_s);
                return -1;
            }
            zcopy_txclose(&zm);
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
        char *_message = new char [strlen(message)+1];
        char replay[buf_size];
        struct zmsghdr zm;
        struct iovec iov[1];
        struct pollfd pfd[1];

        strcpy(_message, message);
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

            zm = {};
            zm.zm_msg.msg_iov = iov;
            zm.zm_msg.msg_iovlen = 1;
            iov[0].iov_base = _message;
            iov[0].iov_len = strlen(message)+1;

            dbg_d("client: writing message: %s", message);

            int bytes = zcopy_tx(s, &zm);
            if (bytes < 0) {
                dbg_d("client: write() failed!");
                close(s);
                return -1;
            }

            pfd[0].fd = s;
            pfd[0].events = POLLIN;
            if (poll(pfd, 1, -1) < 0) {
                dbg_d("client: poll() failed!");
                close(s);
                return -1;
            }

            zm.zm_msg.msg_iov = iov;
            zm.zm_msg.msg_iovlen = 32;

            /* Read max buf_size bytes */
            int bytes2 = zcopy_rx(s, &zm);
            if (bytes2 < 0) {
                dbg_d("client: read() failed!");
                close(s);
                return -1;
            }

            int idx = 0;
            for (int j = 0; j < zm.zm_msg.msg_iovlen; j++) {
                memcpy(&replay[idx], iov[j].iov_base, iov[j].iov_len);
                idx += iov[j].iov_len;
            }
            zcopy_rxgc(&zm);

            replay[bytes2] = '\0';
            dbg_d("client: got replay: %s", replay);

            dbg_d("client: closing socket()");
            if ( close(s) < 0 ) {
                dbg_d("client: close() failed!");
                return -1;
            }

            pfd[0].fd = zm.zm_txfd;
            pfd[0].events = POLLIN;
            if (poll(pfd, 1, -1) < 0) {
                dbg_d("client: poll() failed!");
                close(s);
                return -1;
            }
            zcopy_txclose(&zm);
        }

        dbg_d("client: DONE");
        return 0;
    }

    int run(void)
    {
        _client_result = 0;
        _server_result = 0;

        sched::thread *srv = sched::thread::make([&] {
            _server_result = tcp_server();
        });
        sched::thread *cli = sched::thread::make([&] {
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
#undef dbg_d
