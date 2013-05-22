#include <osv/semaphore.hh>

semaphore::semaphore(unsigned val)
    : _val(val)
    , _mtx(new mutex)
{
}

void semaphore::post()
{
    auto wr = with_lock(*_mtx, [this] () -> wait_record* {
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
    with_lock(*_mtx, [&] {
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

bool semaphore::trywait()
{
    bool ok = false;
    with_lock(*_mtx, [&] {
        if (_val > 0) {
            --_val;
            ok = true;
        }
    });

    return ok;
}




