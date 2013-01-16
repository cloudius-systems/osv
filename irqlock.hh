#ifndef IRQLOCK_HH_
#define IRQLOCK_HH_

#include "arch.hh"

class irq_lock_type {
public:
    static void lock() { arch::irq_disable(); }
    static void unlock() { arch::irq_enable(); }
};

namespace {

irq_lock_type irq_lock;

}

#endif /* IRQLOCK_HH_ */
