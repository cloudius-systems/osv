#ifndef INTERRUPT_HH_
#define INTERRUPT_HH_

#include <functional>
#include "types.hh"

struct msi_message {
    u64 addr;
    u32 data;
};

class msi_interrupt_handler {
public:
    explicit msi_interrupt_handler(std::function<void ()> handler);
    ~msi_interrupt_handler();
    msi_message config();
private:
    unsigned _vector;
    std::function<void ()> _handler;
};

#endif /* INTERRUPT_HH_ */
