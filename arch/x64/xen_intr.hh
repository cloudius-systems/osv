#include <atomic>
#include "sched.hh"
#include <bsd/porting/bus.h>

namespace xen {

class xen_irq {
public:
    explicit xen_irq(driver_intr_t handler, int evtchn, void *args);
    void wake(void) { _irq_pending.store(true, std::memory_order_relaxed); _thread.wake();}
private:
    void do_irq(void);
    driver_intr_t _handler;
    sched::thread _thread;

    void *_args;
    // FIXME: All irq handlers should loop in some condition and then be woken
    // up by us.  Therefore, _irq_pending is really unecessary. All we need is
    // to call wake and make sure the condition is now true. However, not all
    // imported drivers does that, so we use _irq_pending to provide a generic
    // loop for them. But those who do will evaluate now two conditions. We
    // should at least have a way to say that we don't need to bump
    // _irq_pending.
    std::atomic<bool> _irq_pending;
    int _evtchn;
};
}
