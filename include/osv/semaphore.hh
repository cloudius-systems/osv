#ifndef SEMAPHORE_HH_
#define SEMAPHORE_HH_

#include <memory>
#include <osv/mutex.h>
#include <list>
#include <sched.hh>

class semaphore {
public:
    explicit semaphore(unsigned val);
    void post(unsigned units = 1);
    void wait(unsigned units = 1);
    bool trywait(unsigned units = 1);
private:
    unsigned _val;
    std::unique_ptr<mutex> _mtx;
    struct wait_record {
        sched::thread* owner;
        unsigned units;
    };
    std::list<wait_record*> _waiters;
};

#endif /* SEMAPHORE_HH_ */
