#ifndef TST_BSD_TCP1_H
#define TST_BSD_TCP1_H

#include "sched.hh"

extern "C" {
    #include <bsd/sys/sys/socket.h>
    #include <bsd/sys/netinet/in.h>
    #include <bsd/include/arpa/inet.h>
}

#include "debug.hh"
#include "tst-hub.hh"

#define dbg_d(...) logger::instance()->wrt("tst-tcp1", logger_error, __VA_ARGS__)

#define LISTEN_PORT (5555)
#define ITERATIONS (400)

class test_bsd_tcp1 : public unit_tests::vtest {
public:
    test_bsd_tcp1() {}
    virtual ~test_bsd_tcp1() {}

    /* Simple server implementation */
    int tcp_server(void)
    {
        int listen_s;
        int client_s;
        struct sockaddr_in laddr;
        char buf[512] = {};
        int i;

        /* Create listening socket */
        dbg_d("server: creating listening socket...");
        listen_s = socket(AF_INET, SOCK_STREAM, 0);
        if (listen_s < 0) {
            dbg_d("server: socket() failed!");
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

            /* Read max 512 bytes */
            int bytes = read(client_s, &buf, 512);
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
        return 1;
    }

    int tcp_client(void)
    {
        struct sockaddr_in raddr;
        const char * message = "This is a TCP message";
        char replay[512];

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

            int bytes2 = read(s, &replay, 512);
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
        return 1;
    }

    virtual void run(void)
    {
        dbg_d("BSD TCP1 Test - Begin");

        _client_result = 0;
        _server_result = 0;
        sched::thread * current = sched::thread::current();

        sched::detached_thread *srv = new sched::detached_thread([&] {
            _server_result = tcp_server();
            current->wake();
        });
        sched::detached_thread *cli = new sched::detached_thread([&] {
            _client_result = tcp_client();
            current->wake();
        });

        srv->start();
        sleep(1);
        cli->start();

        sched::thread::wait_until([this] {
            return (_client_result && _server_result);
        });

        dbg_d("BSD TCP1 Test - End");
    }

private:
    int _client_result;
    int _server_result;
};

#undef ITERATIONS
#undef LISTEN_PORT
#undef dbg_d

#endif /* !TST_BSD_TCP1_H */
