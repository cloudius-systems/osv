#include "tst-hub.hh"
#include "tst-threads.hh"
#include "tst-malloc.hh"
#include "tst-timer.hh"
#include "tst-devices.hh"
#include "tst-eventlist.hh"
#include "tst-rwlock.hh"
#include "tst-bsd-synch.hh"
#include "tst-queue-mpsc.hh"
#include "tst-mmap.hh"
#include "tst-sockets.hh"
#include "tst-bsd-tcp1.hh"

using namespace unit_tests;

void tests::execute_tests() {
    test_threads threads;
    test_malloc malloc;
    test_timer timer;
    test_devices dev;
    test_eventlist evlist;
    test_rwlock rwlock;
    test_synch synch;
    test_queue_mpsc q1;
    test_mmap mmap;
    test_sockets sockets;
    test_bsd_tcp1 tcp1;

    instance().register_test(&threads);
    instance().register_test(&malloc);
    instance().register_test(&timer);
    instance().register_test(&dev);
    instance().register_test(&evlist);
    instance().register_test(&rwlock);
    instance().register_test(&synch);
    instance().register_test(&q1);
    instance().register_test(&mmap);
    instance().register_test(&sockets);
    instance().register_test(&tcp1);

    instance().run();
}

int main(int ac, char** av)
{
    unit_tests::tests::instance().execute_tests();
    return 0;
}

