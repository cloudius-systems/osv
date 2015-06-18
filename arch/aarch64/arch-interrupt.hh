/*
 * Copyright (C) 2015 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef ARCH_INTERRUPT_HH
#define ARCH_INTERRUPT_HH

#include "exceptions.hh"
#include "gic.hh"

#include <functional>

/* Software-Generated Interrupts */

namespace sched {
    class cpu;
}

class sgi_interrupt : public interrupt {
public:
    sgi_interrupt(enum ipi_id, std::function<void ()>);
    ~sgi_interrupt();

    void send(sched::cpu* cpu);
    void send_allbutself();
};

typedef class sgi_interrupt inter_processor_interrupt;

/* Private Periphereal Interrupts */

class ppi_interrupt : public interrupt {
public:
    ppi_interrupt(gic::irq_type t, unsigned id, std::function<void ()> h);
    ~ppi_interrupt();
private:
    gic::irq_type irq_type;
};

/* Shared Periphereal Interrupts */
class spi_interrupt : public interrupt {
public:
    spi_interrupt(gic::irq_type t, unsigned id, std::function<bool ()> a,
                  std::function<void ()> h);
    ~spi_interrupt();
private:
    gic::irq_type irq_type;
};

#endif /* ARCH_INTERRUPT_HH */
