#include <osv/semaphore.hh>

semaphore::semaphore(unsigned val)
    : _val(val)
{
}

void semaphore::post(unsigned units)
{
    with_lock(_mtx, [=] {
        _val += units;
        auto i = _waiters.begin();
        while (_val > 0 && i != _waiters.end()) {
            auto wr = i++;
            if (wr->units <= _val) {
                _val -= wr->units;
                wr->owner->wake();
                wr->owner = nullptr;
                _waiters.erase(wr);
            }
        }
    });
}

void semaphore::wait(unsigned units)
{
    bool wait = false;
    wait_record wr;
    wr.owner = nullptr;
    with_lock(_mtx, [&] {
        if (_val >= units) {
            _val -= units;
        } else {
            wr.owner = sched::thread::current();
            wr.units = units;
            _waiters.push_back(wr);
            wait = true;
        }
    });

    if (wait)
        sched::thread::wait_until([&] { return !wr.owner; });
}

bool semaphore::trywait(unsigned units)
{
    bool ok = false;
    with_lock(_mtx, [&] {
        if (_val > units) {
            _val -= units;
            ok = true;
        }
    });

    return ok;
}




