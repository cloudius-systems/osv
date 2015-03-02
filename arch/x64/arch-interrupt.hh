/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef ARCH_INTERRUPT_HH
#define ARCH_INTERRUPT_HH

#include <osv/sched.hh>

class inter_processor_interrupt {
public:
    explicit inter_processor_interrupt(std::function<void ()>);
    ~inter_processor_interrupt();
    void send(sched::cpu* cpu);
    void send_allbutself();
private:
    unsigned _vector;
};

class gsi_interrupt {
public:
    void set(unsigned gsi, unsigned vector);
    void clear();
private:
    unsigned _gsi;
};

class gsi_edge_interrupt : public interrupt {
public:
    gsi_edge_interrupt(unsigned id, std::function<void ()> h);
    ~gsi_edge_interrupt();

    void set_vector(unsigned v);
    unsigned get_vector();

    void enable();
    void disable();

private:
    unsigned _vector;
    gsi_interrupt _gsi;
};

class gsi_level_interrupt : public interrupt {
public:
    gsi_level_interrupt(unsigned id, std::function<bool ()> a,
                        std::function<void ()> h);
    ~gsi_level_interrupt();

    void set_vector(shared_vector v);
    shared_vector get_vector();

    void enable();
    void disable();

private:
    shared_vector _vector;
    gsi_interrupt _gsi;
};

#endif /* ARCH_INTERRUPT_HH */
