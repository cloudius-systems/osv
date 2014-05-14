/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef ARCH_HH_
#define ARCH_HH_

#include "processor.hh"

// architecture independent interface for architecture dependent operations

namespace arch {

#define CACHELINE_ALIGNED __attribute__((aligned(64)))
#define INSTR_SIZE_MIN 4
#define ELF_IMAGE_START 0x40090000

inline void irq_disable()
{
    processor::irq_disable();
}

__attribute__((no_instrument_function))
inline void irq_disable_notrace();

inline void irq_disable_notrace()
{
    processor::irq_disable_notrace();
}

inline void irq_enable()
{
    processor::irq_enable();
}

inline void wait_for_interrupt()
{
    processor::wait_for_interrupt();
}

inline void halt_no_interrupts()
{
    processor::halt_no_interrupts();
}

class irq_flag {
public:
    void save() {
        asm volatile("mrs %0, daif;" : "=r"(daif) :: "memory");
    }

    void restore() {
        asm volatile("msr daif, %0" :: "r"(daif) : "memory");
    }
    bool enabled() const {
        return !(daif & processor::daif_i);
    }

private:
    unsigned long daif;
};

class irq_flag_notrace {
public:
    __attribute__((no_instrument_function)) void save();
    __attribute__((no_instrument_function)) void restore();
    __attribute__((no_instrument_function)) bool enabled() const;
private:
    unsigned long daif;
};

inline void irq_flag_notrace::save() {
    asm volatile("mrs %0, daif;" : "=r"(daif) :: "memory");
}

inline void irq_flag_notrace::restore() {
    asm volatile("msr daif, x0" :: "r"(daif) : "memory");
}

inline bool irq_flag_notrace::enabled() const {
    return !(daif & processor::daif_i);
}

inline bool irq_enabled()
{
    irq_flag f;
    f.save();
    return f.enabled();
}

extern bool tls_available() __attribute__((no_instrument_function));

inline bool tls_available()
{
    /* XXX */
    return false;
}

} // namespace arch

#endif /* ARCH_HH_ */
