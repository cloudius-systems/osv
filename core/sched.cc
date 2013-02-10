#include "sched.hh"
#include <list>
#include "mutex.hh"
#include <mutex>
#include "debug.hh"
#include "drivers/clockevent.hh"
#include "irqlock.hh"
#include "align.hh"
#include "drivers/clock.hh"

namespace sched {

std::vector<cpu*> cpus;

thread __thread * s_current;

elf::tls_data tls;

}

#include "arch-switch.hh"

namespace sched {

void schedule_force();

void cpu::schedule(bool yield)
{
    // FIXME: drive by IPI
    handle_incoming_wakeups();
    thread* p = thread::current();
    if (!p->_waiting && !yield) {
        return;
    }
    // FIXME: a proper idle mechanism
    while (runqueue.empty()) {
        barrier();
        handle_incoming_wakeups();
    }
    thread* n = with_lock(irq_lock, [this] {
        auto n = &runqueue.front();
        runqueue.pop_front();
        return n;
    });
    assert(!n->_waiting);
    n->_on_runqueue = false;
    if (n != thread::current()) {
        n->switch_to();
    }
}

void cpu::handle_incoming_wakeups()
{
    for (unsigned i = 0; i < cpus.size(); ++i) {
        incoming_wakeup_queue q;
        incoming_wakeups[i].copy_and_clear(q);
        while (!q.empty()) {
            auto& t = q.front();
            q.pop_front_nonatomic();
            t._on_runqueue = true;
            runqueue.push_back(t);
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
            if (runqueue.empty()) {
                return;
            }
            auto& mig = runqueue.back();
            runqueue.pop_back();
            // we won't race with wake(), since we're not _waiting
            mig._cpu = min;
            min->incoming_wakeups[id].push_front(mig);
            // FIXME: IPI
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
    // FIXME: what about other cpus?
    if (t->_cpu->runqueue.empty()) {
        return;
    }
    t->_cpu->runqueue.push_back(*t);
    t->_on_runqueue = true;
    assert(!t->_waiting);
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

thread::thread(std::function<void ()> func, attr attr, bool main)
    : _func(func)
    , _on_runqueue(!main)
    , _waiting(false)
    , _attr(attr)
    , _terminated(false)
    , _joiner()
{
    with_lock(thread_list_mutex, [this] {
        thread_list.push_back(*this);
    });
    setup_tcb();
    init_stack();
    if (!main) {
        _cpu = current()->tcpu(); // inherit creator's cpu
        _cpu->runqueue.push_back(*this);
    }
}

thread::~thread()
{
    with_lock(thread_list_mutex, [this] {
        thread_list.erase(thread_list.iterator_to(*this));
    });
    debug("thread dtor");
}

void thread::prepare_wait()
{
    _waiting = true;
}

void thread::wake()
{
    // prevent two concurrent wakeups
    if (!_waiting.exchange(false)) {
        return;
    }
    _cpu->incoming_wakeups[cpu::current()->id].push_front(*this);
    // FIXME: IPI
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
    if (!_waiting) {
        return;
    }
    schedule();
}

void thread::sleep_until(u64 abstime)
{
    timer t(*current());
    t.set(abstime);
    wait_until([&] { return t.expired(); });
}

void thread::stop_wait()
{
    _waiting = false;
}

void thread::complete()
{
    _waiting = true;
    _terminated = true;
    if (_joiner) {
        _joiner->wake();
    }
    while (true) {
        schedule();
    }
}

void thread::join()
{
    _joiner = current();
    wait_until([this] { return _terminated; });
}

thread::stack_info thread::get_stack_info()
{
    return _attr.stack;
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
        _t._cpu->timers._list.erase(*this);
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

void init(elf::tls_data tls_data, std::function<void ()> cont)
{
    tls = tls_data;
    thread::attr attr;
    attr.stack = { new char[4096*10], 4096*10 };
    thread t{cont, attr, true};
    t.switch_to_first();
}

}
