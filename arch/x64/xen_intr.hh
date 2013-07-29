#include <atomic>
#include "sched.hh"
#include <bsd/porting/bus.h>

namespace xen {

class xen_irq {
public:
    explicit xen_irq(driver_intr_t handler, void *args);
    virtual ~xen_irq();
    void do_irq(void);
    void wake(void) { _irq_pending++; _thread->wake(); }
private:
    driver_intr_t _handler;
    sched::thread *_thread;
    void *_args;
    std::atomic<int> _irq_pending;
};
}
