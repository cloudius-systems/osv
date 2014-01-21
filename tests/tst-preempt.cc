/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/sched.hh>
#include <osv/debug.hh>

namespace sched {
    // For this to work, we need to implement __tls_get_addr()
    //extern unsigned int __thread preempt_counter;
    // It's much easier, to expose the TLS variable via a function:
    extern unsigned int get_preempt_counter();
}

int main(int argc, char **argv)
{
    debug("Running preemption tests\n");

    // Test 1: check that new threads start preemptable, i.e., with
    // preempt_counter==0. We had a bug where we didn't zero the .tbss
    // section, leading to non-zero preempt_counter initialization.
    assert(sched::get_preempt_counter() == 0);

    auto t1 = new sched::thread([] {
            assert(sched::get_preempt_counter() == 0);
    });
    t1->start();
    delete t1;

    debug("Preemption tests succeeded\n");

}
