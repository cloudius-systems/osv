#include "sched.hh"
#include <list>
#include "mutex.hh"
#include <mutex>
#include "debug.hh"
#include "drivers/clockevent.hh"
#include "irqlock.hh"
#include "align.hh"

namespace sched {

std::list<thread*> runqueue;

std::vector<cpu*> cpus;

thread __thread * s_current;

elf::tls_data tls;

}

#include "arch-switch.hh"

namespace sched {

void schedule_force();

void schedule(bool yield)
{
    thread* p = thread::current();
    if (!p->_waiting && !yield) {
        return;
    }
    // FIXME: a proper idle mechanism
    while (runqueue.empty()) {
        barrier();
    }
    thread* n = with_lock(irq_lock, [] {
        auto n = runqueue.front();
        runqueue.pop_front();
        return n;
    });
    assert(!n->_waiting);
    n->_on_runqueue = false;
    if (n != thread::current()) {
        n->switch_to();
    }
}

void thread::yield()
{
    if (runqueue.empty()) {
        return;
    }
    auto t = current();
    runqueue.push_back(t);
    t->_on_runqueue = true;
    assert(!t->_waiting);
    schedule(true);
}

thread::stack_info::stack_info(void* _begin, size_t _size)
    : begin(_begin), size(_size)
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

thread::thread(std::function<void ()> func, stack_info stack, bool main)
    : _func(func)
    , _on_runqueue(!main)
    , _waiting(false)
    , _stack(stack)
{
    with_lock(thread_list_mutex, [this] {
        thread_list.push_back(*this);
    });
    if (!main) {
        setup_tcb();
        init_stack();
        runqueue.push_back(this);
    } else {
        setup_tcb_main();
        s_current = this;
        switch_to_thread_stack();
        abort();
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
    with_lock(irq_lock, [this] {
        if (!_waiting) {
            return;
        }
        _waiting = false;
        if (!_on_runqueue) {
            _on_runqueue = true;
            runqueue.push_back(this);
        }
    });
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

thread::stack_info thread::get_stack_info()
{
    return _stack;
}

timer_list timers;

timer_list::timer_list()
{
    clock_event->set_callback(this);
}

void timer_list::fired()
{
    timer& tmr = *_list.begin();
    tmr._expired = true;
    tmr._t.wake();
}

timer::timer(thread& t)
    : _t(t)
    , _expired()
{
}

timer::~timer()
{
    cancel();
}

void timer::set(u64 time)
{
    _time = time;
    // FIXME: locking
    timers._list.insert(*this);
    if (timers._list.iterator_to(*this) == timers._list.begin()) {
        clock_event->set(time);
    }
};

void timer::cancel()
{
    // FIXME: locking
    timers._list.erase(*this);
    _expired = false;
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

void init(elf::tls_data tls_data)
{
    tls = tls_data;
}

}
