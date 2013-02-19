#ifndef ARCH_HH_
#define ARCH_HH_

#include "processor.hh"

// namespace arch - architecture independent interface for architecture
//                  dependent operations (e.g. irq_disable vs. cli)

namespace arch {

inline void irq_disable()
{
    processor::cli();
}

inline void irq_enable()
{
    processor::sti();
}

inline void wait_for_interrupt()
{
    processor::sti_hlt();
}

class irq_flag {
public:
    // need to clear the red zone when playing with the stack. also, can't
    // use "m" constraint as it might be addressed relative to %rsp
    void save() {
        asm volatile("sub $128, %%rsp; pushfq; popq %0; add $128, %%rsp" : "=r"(_rflags));
    }
    void restore() {
        asm volatile("sub $128, %%rsp; pushq %0; popfq; add $128, %%rsp" : : "r"(_rflags));
    }
private:
    unsigned long _rflags;
};

}


#endif /* ARCH_HH_ */
