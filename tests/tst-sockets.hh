#ifndef TST_SOCKETS_H
#define TST_SOCKETS_H

#include "sched.hh"

extern "C" {
    #include <bsd/sys/sys/socket.h>
    #include <bsd/sys/netinet/in.h>
    #include <bsd/include/arpa/inet.h>
}

#include "debug.hh"
#include "tst-hub.hh"

#define dbg_d(...)   logger::instance()->wrt("tst-sockets", logger_error, __VA_ARGS__)

#define UT_BUFLEN 512
#define UT_NPACK 5
#define UT_PORT 9930

/*
 * Based on code found here http://www.abc.se/~m6695/udp.html
 */
class socket_test_simple_udp {
public:
    socket_test_simple_udp() {}
    virtual ~socket_test_simple_udp() {}

    int udp_server(void)
    {
        struct sockaddr_in si_me, si_other;
        int s, i;
        socklen_t slen=sizeof(si_other);
        char buf[UT_BUFLEN];

        /* Create UDP Socket */
        if ((s=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))==-1) {
            dbg_d("udp_server() socket() failed %d", s);
            return -1;
        }

        memset((char *) &si_me, 0, sizeof(si_me));
        si_me.sin_family = AF_INET;
        si_me.sin_port = htons(UT_PORT);
        si_me.sin_addr.s_addr = htonl(INADDR_ANY);
        si_me.sin_len = sizeof(struct sockaddr_in);
        if (bind(s, (const sockaddr*)&si_me, sizeof(si_me))==-1) {
            dbg_d("udp_server() bind() failed %d", s);
            return -1;
        }

        for (i=0; i<UT_NPACK; i++) {
            if (recvfrom(s, buf, UT_BUFLEN, 0, (sockaddr*)&si_other, &slen)==-1) {
                dbg_d("recvfrom failed");
                return -1;
            }

            dbg_d("%s:%d says: \"%s\"",
                inet_ntoa(si_other.sin_addr), ntohs(si_other.sin_port), buf);
        }

        close(s);

        dbg_d("udp_server() finished!");
        return 0;
    }

    int udp_client(void)
    {
        struct sockaddr_in si_other;
        int s, i, slen=sizeof(si_other);
        const char *messages[UT_NPACK] = {
            "Irish handcuffs: When a person is carrying beer in both hands.",
            "glasshole: A person who constantly talks to their Google Glass.",
            "please advise: Corporate Jargon for What The Fuck.",
            "trash jenga: When the garbage gets piled so high...",
            "going commandtoe: when you wear shoes without socks"
        };

        if ((s=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))==-1) {
            dbg_d("udp_client() socket() failed %d", s);
            return -1;
        }

        memset((char *) &si_other, 0, sizeof(si_other));
        si_other.sin_family = AF_INET;
        si_other.sin_port = htons(UT_PORT);
        si_other.sin_len = sizeof(struct sockaddr_in);

        if (inet_aton("127.0.0.1", &si_other.sin_addr)==0) {
            dbg_d("udp_client() inet_aton() failed %d", s);
            return -1;
        }

        for (i=0; i<UT_NPACK; i++) {
            dbg_d("Sending packet %d", i);

            if (sendto(s, messages[i], strlen(messages[i])+1, 0,
                (const struct sockaddr*)&si_other, slen)==-1) {
                dbg_d("udp_client() sendto() failed %d", s);
                return -1;
            }
        }

        close(s);
        return 0;
    }

    void run(void)
    {
        sched::detached_thread* t1 = new sched::detached_thread([&] {
            udp_server();
        });

        sched::detached_thread* t2 = new sched::detached_thread([&] {
            udp_client();
        });

        t1->start();
        t2->start();
        sleep(1);
    }

private:
};

class socket_test_openclose {
public:
    socket_test_openclose() {}
    virtual ~socket_test_openclose() {}

    int run(void)
    {
        int error, s;

        dbg_d("OC: Creating a socket...");

        s = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
        dbg_d("OC:     result=%d", s);
        if (s < 0) {
            dbg_d("OC:     socket() failed!");
            return -1;
        }

        dbg_d("OC: Closing socket %d", s);
        error = close(s);
        if (error < 0) {
            dbg_d("OC:     close() failed!");
            return -1;
        }


        return 0;
    }

private:

};

//
// Main tst-hub class
//
class test_sockets : public unit_tests::vtest {
public:
    test_sockets() {}
    virtual ~test_sockets() {}

    virtual void run(void)
    {
        dbg_d("Sockets Test - Begin");

        /* Open - Close test */
        socket_test_openclose oc;
        oc.run();

        /* Simple UDP test */
        socket_test_simple_udp su;
        su.run();

        dbg_d("Sockets Test - End");
    }
};

#undef UT_BUFLEN
#undef UT_NPACK
#undef UT_PORT

#undef dbg_d

#endif
