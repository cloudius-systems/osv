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
#include "smp.hh"
#include "osv/trace.hh"

namespace sched {

tracepoint<thread*> trace_switch("sched_switch", "to %p");
tracepoint<> trace_wait("sched_wait", "");
tracepoint<thread*> trace_wake("sched_wake", "wake %p");
tracepoint<thread*, unsigned> trace_migrate("sched_migrate", "thread=%p cpu=%d");
tracepoint<thread*> trace_queue("sched_queue", "thread=%p");

std::vector<cpu*> cpus;

thread __thread * s_current;

unsigned __thread preempt_counter = CONF_preempt ? 0 : 1;

elf::tls_data tls;

inter_processor_interrupt wakeup_ipi{[] {}};

constexpr u64 vruntime_bias = 4_ms;
constexpr u64 max_slice = 10_ms;

}

#include "arch-switch.hh"

namespace sched {

cpu::cpu()
    : idle_thread([this] { idle(); }, thread::attr(this))
{
}

void cpu::schedule(bool yield)
{

    with_lock(irq_lock, [this] {
        reschedule_from_interrupt();
    });
}

void cpu::reschedule_from_interrupt(bool preempt)
{
    handle_incoming_wakeups();
    auto now = clock::get()->time();
    thread* p = thread::current();
    // avoid cycling through the runqueue if p still has the highest priority
    auto bias = vruntime_bias;
    if (p->_borrow) {
        bias /= 2;  // preempt threads on borrowed time sooner
    }
    if (p->_status == thread::status::running
            && (runqueue.empty()
                || p->_vruntime + now < runqueue.begin()->_vruntime + bias)) {
        return;
    }
    p->_vruntime += now;
    if (p->_status == thread::status::running) {
        p->_status.store(thread::status::queued);
        enqueue(*p, now);
    }
    auto ni = runqueue.begin();
    auto n = &*ni;
    runqueue.erase(ni);
    n->_vruntime -= now;
    assert(n->_status.load() == thread::status::queued);
    n->_status.store(thread::status::running);
    if (n != thread::current()) {
        if (preempt) {
            asm volatile("clts");
            p->_fpu.save();
        }
        trace_switch(n);
        n->switch_to();
        if (preempt) {
            p->_fpu.restore();
        }
    }
}

void cpu::do_idle()
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

void cpu::idle()
{
    while (true) {
        do_idle();
        // FIXME: we don't have an idle priority class yet. so
        // FIXME: yield when we're done and let the scheduler pick
        // FIXME: someone else
        thread::yield();
    }
}

void cpu::handle_incoming_wakeups()
{
    cpu_set queues_with_wakes{incoming_wakeups_mask.fetch_clear()};
    if (!queues_with_wakes) {
        return;
    }
    auto now = clock::get()->time();
    for (auto i : queues_with_wakes) {
        incoming_wakeup_queue q;
        incoming_wakeups[i].copy_and_clear(q);
        while (!q.empty()) {
            auto& t = q.front();
            q.pop_front_nonatomic();
            irq_save_lock_type irq_lock;
            with_lock(irq_lock, [&] {
                t._status.store(thread::status::queued);
                enqueue(t, now);
                t.resume_timers();
            });
        }
    }
}

void cpu::enqueue(thread& t, u64 now)
{
    trace_queue(&t);
    auto head = std::min(t._vruntime, thread::current()->_vruntime + now);
    auto tail = head + max_slice * runqueue.size();
    if (t._vruntime > tail) {
        t._borrow = t._vruntime - tail;
    } else {
        t._borrow = 0;
    }
    runqueue.insert_equal(t);
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
        tmr.set(clock::get()->time() + 100_ms);
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
                    [](thread& t) { return !t._attr.pinned_cpu; });
            if (i == runqueue.rend()) {
                return;
            }
            auto& mig = *i;
            trace_migrate(&mig, min->id);
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
    bool resched = with_lock(irq_lock, [t] {
        t->_cpu->handle_incoming_wakeups();
        // FIXME: what about other cpus?
        if (t->_cpu->runqueue.empty()) {
            return false;
        }
        t->_cpu->runqueue.push_back(*t);
        assert(t->_status.load() == status::running);
        t->_status.store(status::queued);
        return true;
    });
    if (resched) {
        t->_cpu->schedule(true);
    }
}

thread::stack_info::stack_info()
    : begin(nullptr), size(0), deleter(nullptr)
{
}

thread::stack_info::stack_info(void* _begin, size_t _size)
    : begin(_begin), size(_size), deleter(nullptr)
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
    , _vruntime(clock::get()->time())
    , _joiner()
{
    with_lock(thread_list_mutex, [this] {
        thread_list.push_back(*this);
        _id = _s_idgen++;
    });
    setup_tcb();
    init_stack();
    if (main) {
        _vruntime = 0; // simulate the first schedule into this thread
        _status.store(status::running);
    }
}

thread::~thread()
{
    join();
    with_lock(thread_list_mutex, [this] {
        thread_list.erase(thread_list.iterator_to(*this));
    });
    if (_attr.stack.deleter) {
        _attr.stack.deleter(_attr.stack.begin);
    }
    free_tcb();
}

void thread::start()
{
    _cpu = _attr.pinned_cpu ? _attr.pinned_cpu : current()->tcpu();
    assert(_status == status::unstarted);
    _status.store(status::waiting);
    wake();
}

void thread::prepare_wait()
{
    // After setting the thread's status to "waiting", we must not preempt it,
    // as it is no longer in "running" state and therefore will not return.
    preempt_disable();
    assert(_status.load() == status::running);
    _status.store(status::waiting);
}

void thread::wake()
{
    trace_wake(this);
    status old_status = status::waiting;
    if (!_status.compare_exchange_strong(old_status, status::waking)) {
        return;
    }
    preempt_disable();
    unsigned c = cpu::current()->id;
    _cpu->incoming_wakeups[c].push_front(*this);
    _cpu->incoming_wakeups_mask.set(c);
    // FIXME: avoid if the cpu is alive and if the priority does not
    // FIXME: warrant an interruption
    if (_cpu != current()->tcpu()) {
        wakeup_ipi.send(_cpu);
    } else if (arch::irq_enabled() && preempt_counter == 1) { // ignore the preempt_disable() above
        _cpu->schedule();
        // We'll also reschedule at the end of an interrupt if needed
    }
    preempt_enable();
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
    trace_wait();
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
    // Can only re-enable preemption of this thread after it is no longer
    // in "waiting" state (otherwise if preempted, it will not be scheduled
    // in again - this is why we disabled preemption in prepare_wait.
    status old_status = status::waiting;
    if (_status.compare_exchange_strong(old_status, status::running)) {
        preempt_enable();
        return;
    }
    preempt_enable();
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
    if (_status.load() == status::unstarted) {
        // To allow destruction of a thread object before start().
        return;
    }
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

void preempt_disable()
{
    ++preempt_counter;
}

void preempt_enable()
{
    --preempt_counter;
    // FIXME: may need to schedule() here if a high prio thread is waiting
}

void preempt()
{
    if (!preempt_counter) {
        sched::cpu::current()->reschedule_from_interrupt(true);
    }
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
    _thread.start();
}

void detached_thread::reaper::reap()
{
    while (true) {
        with_lock(_mtx, [=] {
            wait_until(_mtx, [=] { return !_zombies.empty(); });
            while (!_zombies.empty()) {
                auto z = _zombies.front();
                _zombies.pop_front();
                z->join();
                delete z;
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
    smp_init();
    thread::attr attr;
    attr.stack = { new char[4096*10], 4096*10 };
    thread t{cont, attr, true};
    t._cpu = smp_initial_find_current_cpu();
    t.switch_to_first();
}

}
