#ifndef SEMAPHORE_HH_
#define SEMAPHORE_HH_

#include <osv/mutex.h>
#include <boost/intrusive/list.hpp>
#include <sched.hh>

class semaphore {
public:
    explicit semaphore(unsigned val);
    void post(unsigned units = 1);
    bool wait(unsigned units = 1, sched::timer* tmr = nullptr);
    bool trywait(unsigned units = 1);
private:
    unsigned _val;
    mutex _mtx;
    struct wait_record : boost::intrusive::list_base_hook<> {
        sched::thread* owner;
        unsigned units;
    };
    boost::intrusive::list<wait_record,
                          boost::intrusive::base_hook<wait_record>,
                          boost::intrusive::constant_time_size<false>> _waiters;
};

#endif /* SEMAPHORE_HH_ */
