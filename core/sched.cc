#include "sched.hh"
#include <list>
#include "mutex.hh"
#include <mutex>
#include "debug.hh"
#include "drivers/clockevent.hh"
#include "irqlock.hh"
#include "align.hh"
#include "drivers/clock.hh"
#include "interrupt.hh"

namespace sched {

std::vector<cpu*> cpus;

thread __thread * s_current;

elf::tls_data tls;

// currently the scheduler will poll right after an interrupt, so no
// need to do anything.
inter_processor_interrupt wakeup_ipi{[] {}};

}

#include "arch-switch.hh"

namespace sched {

void schedule_force();

void cpu::schedule(bool yield)
{
    // FIXME: drive by IPI
    handle_incoming_wakeups();
    thread* p = thread::current();
    if (p->_status.load() == thread::status::running && !yield) {
        return;
    }

    if (runqueue.empty()) {
        idle();
    }

    with_lock(irq_lock, [this] {
        auto n = &runqueue.front();
        runqueue.pop_front();
        assert(n->_status.load() == thread::status::queued
                || n->_status.load() == thread::status::running);
        n->_status.store(thread::status::running);
        if (n != thread::current()) {
            n->switch_to();
        }
    });
}

void cpu::idle()
{
    do {
        // spin for a bit before halting
        for (unsigned ctr = 0; ctr < 100; ++ctr) {
            // FIXME: can we pull threads from loaded cpus?
            handle_incoming_wakeups();
            if (!runqueue.empty()) {
                return;
            }
        }
        std::unique_lock<irq_lock_type> guard(irq_lock);
        handle_incoming_wakeups();
        if (!runqueue.empty()) {
            return;
        }
        guard.release();
        arch::wait_for_interrupt();
        handle_incoming_wakeups(); // auto releases irq_lock
    } while (runqueue.empty());
}

void cpu::handle_incoming_wakeups()
{
    cpu_set queues_with_wakes{incoming_wakeups_mask.fetch_clear()};
    for (auto i : queues_with_wakes) {
        incoming_wakeup_queue q;
        incoming_wakeups[i].copy_and_clear(q);
        while (!q.empty()) {
            auto& t = q.front();
            q.pop_front_nonatomic();
            runqueue.push_back(t);
            with_lock(irq_lock, [&] {
                t.resume_timers();
            });
            t._status.store(thread::status::queued);
        }
    }
}

void cpu::init_on_cpu()
{
    arch.init_on_cpu();
    clock_event->setup_on_cpu();
}

unsigned cpu::load()
{
    return runqueue.size();
}

void cpu::load_balance()
{
    timer tmr(*thread::current());
    while (true) {
        tmr.set(clock::get()->time() + 100000000);
        thread::wait_until([&] { return tmr.expired(); });
        if (runqueue.empty()) {
            continue;
        }
        auto min = *std::min_element(cpus.begin(), cpus.end(),
                [](cpu* c1, cpu* c2) { return c1->load() < c2->load(); });
        if (min == this) {
            continue;
        }
        with_lock(irq_lock, [this, min] {
            auto i = std::find_if(runqueue.rbegin(), runqueue.rend(),
                    [](thread& t) { return !t._attr.pinned; });
            if (i == runqueue.rend()) {
                return;
            }
            auto& mig = *i;
            runqueue.erase(std::prev(i.base()));  // i.base() returns off-by-one
            // we won't race with wake(), since we're not thread::waiting
            assert(mig._status.load() == thread::status::queued);
            mig._status.store(thread::status::waking);
            mig.suspend_timers();
            mig._cpu = min;
            min->incoming_wakeups[id].push_front(mig);
            min->incoming_wakeups_mask.set(id);
            // FIXME: avoid if the cpu is alive and if the priority does not
            // FIXME: warrant an interruption
            wakeup_ipi.send(min);
        });
    }
}

void schedule(bool yield)
{
    cpu::current()->schedule(yield);
}

void thread::yield()
{
    auto t = current();
    // FIXME: drive by IPI
    t->_cpu->handle_incoming_wakeups();
    // FIXME: what about other cpus?
    if (t->_cpu->runqueue.empty()) {
        return;
    }
    with_lock(irq_lock, [t] {
        t->_cpu->runqueue.push_back(*t);
    });
    assert(t->_status.load() == status::running);
    t->_status.store(status::queued);
    t->_cpu->schedule(true);
}

thread::stack_info::stack_info()
    : begin(nullptr), size(0), owned(true)
{
}

thread::stack_info::stack_info(void* _begin, size_t _size)
    : begin(_begin), size(_size), owned(false)
{
    auto end = align_down(begin + size, 16);
    size = static_cast<char*>(end) - static_cast<char*>(begin);
}

mutex thread_list_mutex;
typedef bi::list<thread,
                 bi::member_hook<thread,
                                 bi::list_member_hook<>,
                                 &thread::_thread_list_link>
                > thread_list_type;
thread_list_type thread_list;
unsigned long thread::_s_idgen;

thread::thread(std::function<void ()> func, attr attr, bool main)
    : _func(func)
    , _status(status::unstarted)
    , _attr(attr)
    , _timers_need_reload()
    , _joiner()
{
    with_lock(thread_list_mutex, [this] {
        thread_list.push_back(*this);
        _id = _s_idgen++;
    });
    setup_tcb();
    init_stack();
    if (!main) {
        _cpu = current()->tcpu(); // inherit creator's cpu
    } else {
        _status.store(status::running);
    }
}

thread::~thread()
{
    with_lock(thread_list_mutex, [this] {
        thread_list.erase(thread_list.iterator_to(*this));
    });
    debug("thread dtor");
}

void thread::start()
{
    assert(_status == status::unstarted);
    _status.store(status::waiting);
    wake();
}

void thread::prepare_wait()
{
    assert(_status.load() == status::running);
    _status.store(status::waiting);
}

void thread::wake()
{
    status old_status = status::waiting;
    if (!_status.compare_exchange_strong(old_status, status::waking)) {
        return;
    }
    unsigned c = cpu::current()->id;
    _cpu->incoming_wakeups[c].push_front(*this);
    _cpu->incoming_wakeups_mask.set(c);
    // FIXME: avoid if the cpu is alive and if the priority does not
    // FIXME: warrant an interruption
    if (_cpu != current()->tcpu()) {
        wakeup_ipi.send(_cpu);
    }
}

void thread::main()
{
    _func();
}

thread* thread::current()
{
    return sched::s_current;
}

void thread::wait()
{
    schedule(true);
}

void thread::sleep_until(u64 abstime)
{
    timer t(*current());
    t.set(abstime);
    wait_until([&] { return t.expired(); });
}

void thread::stop_wait()
{
    status old_status = status::waiting;
    if (_status.compare_exchange_strong(old_status, status::running)) {
        return;
    }
    while (_status.load() == status::waking) {
        schedule(true);
    }
    assert(_status.load() == status::running);
}

void thread::complete()
{
    _status.store(status::terminated);
    if (_joiner) {
        _joiner->wake();
    }
    while (true) {
        schedule();
    }
}

void thread::suspend_timers()
{
    if (_timers_need_reload) {
        return;
    }
    _timers_need_reload = true;
    _cpu->timers.suspend(_active_timers);
}

void thread::resume_timers()
{
    if (!_timers_need_reload) {
        return;
    }
    _timers_need_reload = false;
    _cpu->timers.resume(_active_timers);
}

void thread::join()
{
    _joiner = current();
    wait_until([this] { return _status.load() == status::terminated; });
}

thread::stack_info thread::get_stack_info()
{
    return _attr.stack;
}

unsigned long thread::id()
{
    return _id;
}

timer_list::callback_dispatch::callback_dispatch()
{
    clock_event->set_callback(this);
}

void timer_list::fired()
{
    auto now = clock::get()->time();
    auto i = _list.begin();
    while (i != _list.end() && i->_time < now) {
        auto j = i++;
        j->expire();
        _list.erase(j);
    }
    if (!_list.empty()) {
        clock_event->set(_list.begin()->_time);
    }
}

// call with irq disabled
void timer_list::suspend(bi::list<timer>& timers)
{
    for (auto& t : timers) {
        assert(!t._expired);
        _list.erase(_list.iterator_to(t));
    }
}

// call with irq disabled
void timer_list::resume(bi::list<timer>& timers)
{
    bool rearm = false;
    for (auto& t : timers) {
        assert(!t._expired);
        auto i = _list.insert(t).first;
        rearm |= i == _list.begin();
    }
    if (rearm) {
        clock_event->set(_list.begin()->_time);
    }
}

void timer_list::callback_dispatch::fired()
{
    cpu::current()->timers.fired();
}

timer_list::callback_dispatch timer_list::_dispatch;

timer::timer(thread& t)
    : _t(t)
    , _expired()
{
}

timer::~timer()
{
    cancel();
}

void timer::expire()
{
    _expired = true;
    _t._active_timers.erase(_t._active_timers.iterator_to(*this));
    _t.wake();
}

void timer::set(u64 time)
{
    _expired = false;
    _time = time;
    with_lock(irq_lock, [=] {
        auto& timers = _t._cpu->timers;
        timers._list.insert(*this);
        _t._active_timers.push_back(*this);
        if (timers._list.iterator_to(*this) == timers._list.begin()) {
            clock_event->set(time);
        }
    });
};

void timer::cancel()
{
    with_lock(irq_lock, [=] {
        if (_expired) {
            return;
        }
        _t._active_timers.erase(_t._active_timers.iterator_to(*this));
        _t._cpu->timers._list.erase(_t._cpu->timers._list.iterator_to(*this));
        _expired = false;
    });
    // even if we remove the first timer, allow it to expire rather than
    // reprogramming the timer
}

bool timer::expired() const
{
    return _expired;
}

bool operator<(const timer& t1, const timer& t2)
{
    if (t1._time < t2._time) {
        return true;
    } else if (t1._time == t2._time) {
        return &t1 < &t2;
    } else {
        return false;
    }
}

class detached_thread::reaper {
public:
    reaper();
    void reap();
    void add_zombie(detached_thread* z);
private:
    mutex _mtx;
    std::list<detached_thread*> _zombies;
    thread _thread;
};

detached_thread::reaper::reaper()
    : _mtx{}, _zombies{}, _thread([=] { reap(); })
{
}

void detached_thread::reaper::reap()
{
    while (true) {
        wait_until([=] { return !_zombies.empty(); }); // FIXME: locking?
        with_lock(_mtx, [=] {
            while (!_zombies.empty()) {
                auto z = _zombies.front();
                _zombies.pop_front();
                z->join();
            }
        });
    }
}

void detached_thread::reaper::add_zombie(detached_thread* z)
{
    with_lock(_mtx, [=] {
        _zombies.push_back(z);
        _thread.wake();
    });
}

detached_thread::detached_thread(std::function<void ()> f)
    : thread([=] { f(); _s_reaper->add_zombie(this); })
{
}

detached_thread::~detached_thread()
{
}

detached_thread::reaper *detached_thread::_s_reaper;

void init_detached_threads_reaper()
{
    detached_thread::_s_reaper = new detached_thread::reaper;
}

void init(elf::tls_data tls_data, std::function<void ()> cont)
{
    tls = tls_data;
    thread::attr attr;
    attr.stack = { new char[4096*10], 4096*10 };
    thread t{cont, attr, true};
    t.switch_to_first();
}

}
