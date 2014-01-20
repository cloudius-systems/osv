/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef OSV_SIGNAL_HH_
#define OSV_SIGNAL_HH_

#include <osv/error.h>
#include <sched.hh>

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

#endif /* OSV_SIGNAL_HH_ */
