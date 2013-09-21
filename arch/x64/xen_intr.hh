/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <atomic>
#include "sched.hh"
#include <bsd/porting/bus.h>
#include <osv/percpu.hh>

namespace xen {

class xen_irq {
public:
    explicit xen_irq();
    void wake(void){ (*_thread)->wake(); }
    static void register_irq(int vector, driver_intr_t handler, void *arg);
private:
    void do_irq(void);
    void cpu_init() { _cpu_init(sched::cpu::current()); };
    sched::cpu::notifier _cpu_notifier;
    void _cpu_init(sched::cpu *c);

    percpu <sched::thread *> _thread;
};
}
