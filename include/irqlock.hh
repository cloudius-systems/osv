#ifndef IRQLOCK_HH_
#define IRQLOCK_HH_

#include "arch.hh"

class irq_lock_type {
public:
    static void lock() { arch::irq_disable(); }
    static void unlock() { arch::irq_enable(); }
};

class irq_save_lock_type {
public:
    void lock();
    void unlock();
private:
    arch::irq_flag _flags;
};


inline void irq_save_lock_type::lock()
{
    _flags.save();
    arch::irq_disable();
}

inline void irq_save_lock_type::unlock()
{
    _flags.restore();
}

extern irq_lock_type irq_lock;

#endif /* IRQLOCK_HH_ */
