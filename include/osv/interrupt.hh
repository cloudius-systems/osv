/*
 * Copyright (C) 2015 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

/* this is an abstract class for interrupts, archs can derive
 * from this one to model their interrupts.
 *
 * interrupt_id is an interrupt identifier,
 * (example: for x64 gsi, for aarch64 the arm gic interrupt id),
 *
 * ack is the interrupt ack routine.
 *   The ack routine is supposed to verify and ack the interrupt status
 *   from the device and return true if an interrupt is pending for that
 *   device. This is necessary in particular for shared irqs.
 *   For irqs which do not require any ack, a convenience constructor is
 *   available which provides a default ack [] { return true; } lambda.
 *
 * Derived classes are supposed to register into the interrupt table
 * in the constructor, and deregister in the destructor.
 */

#ifndef INTERRUPT_HH
#define INTERRUPT_HH

#include <functional>

class interrupt {
public:
    interrupt(unsigned id, std::function<bool ()> a, std::function<void ()> h)
        : interrupt_id(id), ack(a), handler(h) {};
    interrupt(unsigned id, std::function<void ()> h)
        : interrupt(id, [] { return true; }, h) {};

    virtual ~interrupt() {};

    unsigned get_id() { return interrupt_id; }
    std::function<void (void)> get_handler() { return handler; }
    std::function<bool (void)> get_ack() { return ack; }

protected:
    unsigned interrupt_id;
    std::function<bool (void)> ack;
    std::function<void (void)> handler;
};

enum ipi_id {
    IPI_WAKEUP,
    IPI_TLB_FLUSH,
    IPI_SAMPLER_START,
    IPI_SAMPLER_STOP,
};

#include "arch-interrupt.hh"

#endif /* INTERRUPT_HH */
