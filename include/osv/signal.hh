/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef OSV_SIGNAL_HH_
#define OSV_SIGNAL_HH_

#include <osv/error.h>
#include <osv/sched.hh>

class signal_catcher {
public:
    signal_catcher() { sched::thread::current()->interrupted(false); }
    error result() { return interrupted() ? error(EINTR) : error(); }
    bool interrupted() { return sched::thread::current()->interrupted(); }
    void wait() { sched::thread::wait_for(*this); }
private:
    friend class sched::wait_object<signal_catcher>;
};

namespace sched {

template <>
class wait_object<signal_catcher> {
public:
    wait_object(signal_catcher& sc, mutex* mtx = nullptr) {}
    void arm() {}
    void disarm() {}
    bool poll() const { return sched::thread::current()->interrupted(); }
};

}

// signalfd delivery hook (libc/signalfd.cc).  Called by kill() when a signal is
// raised: returns true if a signalfd consumed the signal, in which case the
// caller must not run the default action or a handler.
extern "C" bool osv_signalfd_deliver(int signo);

#endif /* OSV_SIGNAL_HH_ */
