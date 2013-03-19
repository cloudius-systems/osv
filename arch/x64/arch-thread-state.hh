#ifndef ARCH_THREAD_STATE_HH_
#define ARCH_THREAD_STATE_HH_

struct thread_state {
    void* rsp;
    void* rbp;
    void* rip;
};

#endif /* ARCH_THREAD_STATE_HH_ */
