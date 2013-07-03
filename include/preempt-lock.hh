#ifndef __PREEMPT_LOCK_H__
#define __PREEMPT_LOCK_H__

#include <sched.hh>

class preempt_lock_t {
public:
    void lock() { sched::preempt_disable(); }
    void unlock() { sched::preempt_enable(); }
};

namespace {
    preempt_lock_t preempt_lock;
}

#endif // !__PREEMPT_LOCK_H__

