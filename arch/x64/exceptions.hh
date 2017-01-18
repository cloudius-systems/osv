/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef EXCEPTIONS_HH
#define EXCEPTIONS_HH

#include <stdint.h>
#include <functional>
#include <osv/types.h>
#include <osv/rcu.hh>
#include <osv/mutex.h>
#include <vector>

class gsi_edge_interrupt;
class gsi_level_interrupt;
class inter_processor_interrupt;

struct exception_frame {
    ulong r15;
    ulong r14;
    ulong r13;
    ulong r12;
    ulong r11;
    ulong r10;
    ulong r9;
    ulong r8;
    ulong rbp;
    ulong rdi;
    ulong rsi;
    ulong rdx;
    ulong rcx;
    ulong rbx;
    ulong rax;
    u16 error_code;
    ulong rip;
    ulong cs;
    ulong rflags;
    ulong rsp;
    ulong ss;

    void *get_pc(void) { return (void*)rip; }
    unsigned int get_error(void) { return error_code; }
};

extern __thread exception_frame* current_interrupt_frame;

struct shared_vector {
    unsigned vector;
    unsigned id;
    shared_vector(unsigned v, unsigned i)
        : vector(v), id(i)
    {};
};

class interrupt_descriptor_table {
public:
    interrupt_descriptor_table();
    void load_on_cpu();
    void register_interrupt(inter_processor_interrupt *interrupt);
    void unregister_interrupt(inter_processor_interrupt *interrupt);
    void register_interrupt(gsi_edge_interrupt *interrupt);
    void unregister_interrupt(gsi_edge_interrupt *interrupt);
    void register_interrupt(gsi_level_interrupt *interrupt);
    void unregister_interrupt(gsi_level_interrupt *interrupt);
    void invoke_interrupt(unsigned vector);

    /* TODO: after merge of MSI and Xen callbacks as interrupt class,
     * exposing these as 'public' should not be necessary anymore.
     */
    unsigned register_interrupt_handler(std::function<bool ()> pre_eoi,
                                        std::function<void ()> eoi,
                                        std::function<void ()> post_eoi);

    /* register_handler is a simplified way to call register_interrupt_handler
     * with no pre_eoi, and apic eoi.
     */
    unsigned register_handler(std::function<void ()> post_eoi);
    void unregister_handler(unsigned vector);

private:
    enum {
        type_intr_gate = 14,
    };
    enum {
        s_special = 0,
    };
    struct idt_entry {
        u16 offset0;
        u16 selector;
        u8 ist : 3;
        u8 res0 : 5;
        u8 type : 4;
        u8 s : 1;
        u8 dpl : 2;
        u8 p : 1;
        u16 offset1;
        u32 offset2;
        u32 res1;
    } __attribute__((aligned(16)));
    void add_entry(unsigned vec, unsigned ist, void (*handler)());
    idt_entry _idt[256];
    struct handler {
        handler(handler *h, unsigned d)
        {
            if (h) {
                *this = *h;
            }
            for (unsigned i = 0; i < size(); i++) {
                if (ids[i] == d) {
                    ids.erase(ids.begin() + i);
                    pre_eois.erase(pre_eois.begin() + i);
                    post_eois.erase(post_eois.begin() + i);
                    break;
                }
            }
        }

        handler(handler *h,
                std::function<bool ()> _pre_eoi,
                std::function<void ()> _eoi,
                std::function<void ()> _post_eoi)
        {
            if (h) {
                *this = *h;
            }
            eoi = _eoi;
            ids.push_back(id++);
            pre_eois.push_back(_pre_eoi);
            post_eois.push_back(_post_eoi);
        }

        unsigned size()
        {
            return ids.size();
        }

        std::vector<std::function<bool ()>> pre_eois;
        std::function<void ()> eoi;
        std::vector<std::function<void ()>> post_eois;
        std::vector<unsigned> ids;
        unsigned id;
        unsigned gsi;
    };
    osv::rcu_ptr<handler> _handlers[256];
    mutex _lock;

    shared_vector register_level_triggered_handler(unsigned gsi,
                                                   std::function<bool ()> pre_eoi,
                                                   std::function<void ()> post_eoi);

    void unregister_level_triggered_handler(shared_vector v);
};

extern interrupt_descriptor_table idt;

extern "C" {
    void page_fault(exception_frame* ef);
}

bool fixup_fault(exception_frame*);

#endif
