#ifndef TST_SOCKETS_H
#define TST_SOCKETS_H

extern "C" {
#include <sys/socket.h>
#include <netinet/in.h>
}

#include "debug.hh"
#include "tst-hub.hh"

#define dbg_d(...)   logger::instance()->wrt("tst-sockets", logger_error, __VA_ARGS__)

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

        dbg_d("Sockets Test - End");
    }
};

#undef dbg_d

#endif
