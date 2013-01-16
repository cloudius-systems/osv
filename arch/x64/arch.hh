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

}


#endif /* ARCH_HH_ */
