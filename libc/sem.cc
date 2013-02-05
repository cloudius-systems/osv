#include <semaphore.h>
#include "sched.hh"
#include "mutex.hh"

// FIXME: smp safety

class semaphore {
public:
    explicit semaphore(unsigned val);
    void post();
    void wait();
private:
    unsigned _val;
    spinlock _mtx;
    struct wait_record {
        sched::thread* owner;
    };
    std::list<wait_record*> _waiters;
};

semaphore::semaphore(unsigned val)
    : _val(val)
{
}

void semaphore::post()
{
    auto wr = with_lock(_mtx, [this] () -> wait_record* {
        if (_waiters.empty()) {
            ++_val;
            return nullptr;
        }
        auto wr = _waiters.front();
        _waiters.pop_front();
        return wr;
    });
    if (wr) {
        auto t = wr->owner;
        wr->owner = nullptr;
        t->wake();
    }
}

void semaphore::wait()
{
    bool wait = false;
    wait_record wr;
    wr.owner = nullptr;
    with_lock(_mtx, [&] {
        if (_val > 0) {
            --_val;
        } else {
            wr.owner = sched::thread::current();
            _waiters.push_back(&wr);
            wait = true;
        }
    });

    if (wait)
        sched::thread::wait_until([&] { return !wr.owner; });
}

semaphore* from_libc(sem_t* p)
{
    return reinterpret_cast<semaphore*>(p);
}

int sem_init(sem_t* s, int pshared, unsigned val)
{
    new (s) semaphore(val);
    return 0;
}

int sem_post(sem_t* s)
{
    from_libc(s)->post();
    return 0;
}

int sem_wait(sem_t* s)
{
    from_libc(s)->wait();
    return 0;
}
