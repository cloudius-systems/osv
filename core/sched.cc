#include "sched.hh"
#include <list>
#include <osv/mutex.h>
#include <mutex>
#include "debug.hh"
#include "drivers/clockevent.hh"
#include "irqlock.hh"
#include "align.hh"
#include "drivers/clock.hh"
#include "interrupt.hh"
#include "smp.hh"
#include "osv/trace.hh"
#include <osv/percpu.hh>

__thread void* percpu_base;

extern char _percpu_start[], _percpu_end[], _percpu_sec_end[];

namespace sched {

tracepoint<1001, thread*, s64, s64> trace_switch("sched_switch", "to %p vold=%d vnew=%d");
tracepoint<1002> trace_wait("sched_wait", "");
tracepoint<1003, thread*> trace_wake("sched_wake", "wake %p");
tracepoint<1004, thread*, unsigned> trace_migrate("sched_migrate", "thread=%p cpu=%d");
tracepoint<1005, thread*> trace_queue("sched_queue", "thread=%p");
tracepoint<1006> trace_preempt("sched_preempt", "");
TRACEPOINT(trace_timer_set, "timer=%p time=%d", timer_base*, s64);
TRACEPOINT(trace_timer_cancel, "timer=%p", timer_base*);
TRACEPOINT(trace_timer_fired, "timer=%p", timer_base*);

std::vector<cpu*> cpus;

thread __thread * s_current;

unsigned __thread preempt_counter = 1;
bool __thread need_reschedule = false;

elf::tls_data tls;

inter_processor_interrupt wakeup_ipi{[] {}};

constexpr s64 vruntime_bias = 4_ms;
constexpr s64 max_slice = 10_ms;
constexpr s64 context_switch_penalty = 10_us;

mutex cpu::notifier::_mtx;
std::list<cpu::notifier*> cpu::notifier::_notifiers __attribute__((init_priority(300)));

}

#include "arch-switch.hh"

namespace sched {

class thread::reaper {
public:
    reaper();
    void reap();
    void add_zombie(thread* z);
private:
    mutex _mtx;
    std::list<thread*> _zombies;
    thread _thread;
};

cpu::cpu(unsigned _id)
    : id(_id)
    , preemption_timer(*this)
    , idle_thread()
    , terminating_thread(nullptr)
    , running_since(clock::get()->time())
{
    auto max_size = _percpu_sec_end - _percpu_end;
    auto pcpu_size = _percpu_end - _percpu_start;
    assert(pcpu_size * (id + 1) <= max_size);
    percpu_base = _percpu_end + id * pcpu_size;
    memcpy(percpu_base, _percpu_start, pcpu_size);
    percpu_base -= reinterpret_cast<size_t>(_percpu_start);
}

void cpu::init_idle_thread()
{
    idle_thread = new thread([this] { idle(); }, thread::attr(this));
    idle_thread->_vruntime = std::numeric_limits<s64>::max();
}

void cpu::schedule(bool yield)
{

    with_lock(irq_lock, [this] {
        reschedule_from_interrupt();
    });
}

void cpu::reschedule_from_interrupt(bool preempt)
{
    need_reschedule = false;
    handle_incoming_wakeups();
    auto now = clock::get()->time();
    thread* p = thread::current();
    // avoid cycling through the runqueue if p still has the highest priority
    auto bias = vruntime_bias;
    s64 current_run = now - running_since;
    if (p->_vruntime + current_run < 0) { // overflow (idle thread)
        current_run = 0;
    }
    if (p->_status == thread::status::running
            && (runqueue.empty()
                || p->_vruntime + current_run < runqueue.begin()->_vruntime + bias)) {
        update_preemption_timer(p, now, current_run);
        return;
    }
    p->_vruntime += current_run;
    if (p->_status == thread::status::running) {
        p->_status.store(thread::status::queued);
        enqueue(*p);
    }
    auto ni = runqueue.begin();
    auto n = &*ni;
    runqueue.erase(ni);
    running_since = now;
    assert(n->_status.load() == thread::status::queued);
    n->_status.store(thread::status::running);
    if (n != thread::current()) {
        if (preempt) {
            trace_preempt();
            p->_fpu.save();
        }
        if (p->_status.load(std::memory_order_relaxed) == thread::status::queued
                && p != idle_thread) {
            n->_vruntime += context_switch_penalty;
        }
        trace_switch(n, p->_vruntime, n->_vruntime);
        update_preemption_timer(n, now, 0);
        n->switch_to();
        if (preempt) {
            p->_fpu.restore();
        }
        if (p->_cpu->terminating_thread) {
            p->_cpu->terminating_thread->unref();
            p->_cpu->terminating_thread = nullptr;
        }
    }
}

void cpu::update_preemption_timer(thread* current, s64 now, s64 run)
{
    preemption_timer.cancel();
    if (runqueue.empty()) {
        return;
    }
    auto& t = *runqueue.begin();
    auto delta = t._vruntime - (current->_vruntime + run);
    auto expire = now + delta + vruntime_bias;
    if (expire > 0) {
        // avoid idle thread related overflow
        preemption_timer.set(expire);
    }
}

void cpu::timer_fired()
{
    // nothing to do, preemption will happen if needed
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
        arch::wait_for_interrupt(); // this unlocks irq_lock
        handle_incoming_wakeups();
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
    for (auto i : queues_with_wakes) {
        incoming_wakeup_queue q;
        incoming_wakeups[i].copy_and_clear(q);
        while (!q.empty()) {
            auto& t = q.front();
            q.pop_front_nonatomic();
            irq_save_lock_type irq_lock;
            with_lock(irq_lock, [&] {
                t._status.store(thread::status::queued);
                enqueue(t, true);
                t.resume_timers();
            });
        }
    }
}

void cpu::enqueue(thread& t, bool waking)
{
    trace_queue(&t);
    if (waking) {
        // If a waking thread has a really low vruntime, allow it only
        // one extra timeslice; otherwise it would dominate the runqueue
        // and starve out other threads
        auto current = thread::current();
        if (current != idle_thread) {
            auto head = current->_vruntime - max_slice;
            t._vruntime = std::max(t._vruntime, head);
        }
    }
    // special treatment for idle thread: make sure it is in the back of the queue
    if (&t == idle_thread) {
        t._vruntime = thread::max_vruntime;
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
    notifier::fire();
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
            mig.remote_thread_local_var(::percpu_base) = min->percpu_base;
            min->incoming_wakeups[id].push_front(mig);
            min->incoming_wakeups_mask.set(id);
            // FIXME: avoid if the cpu is alive and if the priority does not
            // FIXME: warrant an interruption
            wakeup_ipi.send(min);
        });
    }
}

cpu::notifier::notifier(std::function<void ()> cpu_up)
    : _cpu_up(cpu_up)
{
    with_lock(_mtx, [this] { _notifiers.push_back(this); });
}

cpu::notifier::~notifier()
{
    with_lock(_mtx, [this] { _notifiers.remove(this); });
}

void cpu::notifier::fire()
{
    with_lock(_mtx, [] {
        for (auto n : _notifiers) {
            n->_cpu_up();
        }
    });
}

void schedule(bool yield)
{
    cpu::current()->schedule(yield);
}

void thread::yield()
{
    auto t = current();
    std::lock_guard<irq_lock_type> guard(irq_lock);
    // FIXME: drive by IPI
    t->_cpu->handle_incoming_wakeups();
    // FIXME: what about other cpus?
    if (t->_cpu->runqueue.empty()) {
        return;
    }
    // TODO: need to give up some vruntime (move to borrow) so we're last
    // on the queue, and then we can use push_back()
    t->_cpu->runqueue.insert_equal(*t);
    assert(t->_status.load() == status::running);
    t->_status.store(status::queued);
    t->_cpu->reschedule_from_interrupt(false);
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

void thread::stack_info::default_deleter(thread::stack_info si)
{
    free(si.begin);
}

mutex thread_list_mutex;
typedef bi::list<thread,
                 bi::member_hook<thread,
                                 bi::list_member_hook<>,
                                 &thread::_thread_list_link>
                > thread_list_type;
thread_list_type thread_list;
unsigned long thread::_s_idgen;

void* thread::do_remote_thread_local_var(void* var)
{
    auto tls_cur = static_cast<char*>(current()->_tcb->tls_base);
    auto tls_this = static_cast<char*>(this->_tcb->tls_base);
    auto offset = static_cast<char*>(var) - tls_cur;
    return tls_this + offset;
}

template <typename T>
T& thread::remote_thread_local_var(T& var)
{
    return *static_cast<T*>(do_remote_thread_local_var(&var));
}

thread::thread(std::function<void ()> func, attr attr, bool main)
    : _func(func)
    , _status(status::unstarted)
    , _attr(attr)
    , _vruntime(main ? 0 : current()->_vruntime)
    , _ref_counter(1)
    , _joiner()
{
    with_lock(thread_list_mutex, [this] {
        thread_list.push_back(*this);
        _id = _s_idgen++;
    });
    setup_tcb();
    // setup s_current before switching to the thread, so interrupts
    // can call thread::current()
    // remote_thread_local_var() doesn't work when there is no current
    // thread, so don't do this for main threads (switch_to_first will
    // do that for us instead)
    if (!main) {
        remote_thread_local_var(s_current) = this;
    }
    init_stack();
    if (_attr.detached) {
        // assumes detached threads directly on heap, not as member.
        // if untrue, or need a special deleter, the user must call
        // set_cleanup() with whatever cleanup needs to be done.
        set_cleanup([=] { delete this; });
    }
    if (main) {
        _cpu = attr.pinned_cpu;
        _status.store(status::running);
    }
}

thread::~thread()
{
    if (!_attr.detached) {
        join();
    }
    with_lock(thread_list_mutex, [this] {
        thread_list.erase(thread_list.iterator_to(*this));
    });
    if (_attr.stack.deleter) {
        _attr.stack.deleter(_attr.stack);
    }
    free_tcb();
}

void thread::start()
{
    _cpu = _attr.pinned_cpu ? _attr.pinned_cpu : current()->tcpu();
    remote_thread_local_var(percpu_base) = _cpu->percpu_base;
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

void thread::ref()
{
    _ref_counter.fetch_add(1);
}

// The _ref_counter is initialized to 1, and reduced by 1 in complete().
// Whomever calls unref() and reduces it to 0 gets the honor of ending this
// thread. This can happen in complete() or somewhere using ref()/unref()).
void thread::unref()
{
    if (_ref_counter.fetch_add(-1) == 1) {
        // thread can't unref() itself, because if it decides to wake joiner,
        // it will delete the stack it is currently running on.
        assert(thread::current() != this);

        // FIXME: we have a problem in case of a race between join() and the
        // thread's completion. Here we can see _joiner==0 and not notify
        // anyone, but at the same time join() decided to go to sleep (because
        // status is not yet status::terminated) and we'll never wake it.
        if (_joiner) {
            _joiner->wake_with([&] { _status.store(status::terminated); });
        } else {
            _status.store(status::terminated);
        }
    }
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
    } else {
        need_reschedule = true;
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

void thread::sleep_until(s64 abstime)
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
    if (_attr.detached) {
        _s_reaper->add_zombie(this);
    }
    // If this thread gets preempted after changing status it will never be
    // scheduled again to set terminating_thread. So must disable preemption.
    preempt_disable();
    _status.store(status::terminating);
    // We want to run unref() here, but can't because it cause the stack we're
    // running on to be deleted. Instead, set a _cpu field telling the next
    // thread running on this cpu to do the unref() for us.
    if (_cpu->terminating_thread) {
        assert(_cpu->terminating_thread != this);
        _cpu->terminating_thread->unref();
    }
    _cpu->terminating_thread = this;
    // The thread is now in the "terminating" state, so on call to schedule()
    // it will never get to run again.
    while (true) {
        schedule();
    }
}

/*
 * Exit a thread.  Doesn't unwind any C++ ressources, and should
 * only be used to implement higher level threading abstractions.
 */
void thread::exit()
{
    thread* t = current();

    t->complete();
}

void timer_base::client::suspend_timers()
{
    if (_timers_need_reload) {
        return;
    }
    _timers_need_reload = true;
    cpu::current()->timers.suspend(_active_timers);
}

void timer_base::client::resume_timers()
{
    if (!_timers_need_reload) {
        return;
    }
    _timers_need_reload = false;
    cpu::current()->timers.resume(_active_timers);
}

void thread::join()
{
    if (_status.load() == status::unstarted) {
        // To allow destruction of a thread object before start().
        return;
    }
    _joiner = current();
    wait_until([this] { return _status.load() == status::terminated; });
    // probably unneeded, but don't execute an std::function<> which may
    // be deleting itself
    auto cleanup = _cleanup;
    if (cleanup) {
        cleanup();
    }
}

thread::stack_info thread::get_stack_info()
{
    return _attr.stack;
}

void thread::set_cleanup(std::function<void ()> cleanup)
{
    _cleanup = cleanup;
}

void thread::timer_fired()
{
    wake();
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
    if (preemptable() && need_reschedule && arch::irq_enabled()) {
        schedule();
    }
}

bool preemptable()
{
    return (!preempt_counter);
}

unsigned int get_preempt_counter()
{
    return preempt_counter;
}

void preempt()
{
    if (preemptable()) {
        sched::cpu::current()->reschedule_from_interrupt(true);
    } else {
        // preempt_enable() will pick this up eventually
        need_reschedule = true;
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
    _last = std::numeric_limits<s64>::max();
    while (i != _list.end() && i->_time <= now) {
        auto j = i++;
        j->expire();
        _list.erase(j);
    }
    if (!_list.empty()) {
        rearm();
    }
}

void timer_list::rearm()
{
    auto t = _list.begin()->_time;
    if (t < _last) {
        _last = t;
        clock_event->set(t);
    }
}

// call with irq disabled
void timer_list::suspend(bi::list<timer_base>& timers)
{
    for (auto& t : timers) {
        assert(t._state == timer::state::armed);
        _list.erase(_list.iterator_to(t));
    }
}

// call with irq disabled
void timer_list::resume(bi::list<timer_base>& timers)
{
    bool rearm = false;
    for (auto& t : timers) {
        assert(t._state == timer::state::armed);
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

timer_base::timer_base(timer_base::client& t)
    : _t(t)
{
}

timer_base::~timer_base()
{
    cancel();
}

void timer_base::expire()
{
    trace_timer_fired(this);
    _state = state::expired;
    _t._active_timers.erase(_t._active_timers.iterator_to(*this));
    _t.timer_fired();
}

void timer_base::set(s64 time)
{
    trace_timer_set(this, time);
    _state = state::armed;
    _time = time;
    irq_save_lock_type irq_lock;
    with_lock(irq_lock, [=] {
        auto& timers = cpu::current()->timers;
        timers._list.insert(*this);
        _t._active_timers.push_back(*this);
        if (timers._list.iterator_to(*this) == timers._list.begin()) {
            timers.rearm();
        }
    });
};

void timer_base::cancel()
{
    if (_state == state::free) {
        return;
    }
    trace_timer_cancel(this);
    irq_save_lock_type irq_lock;
    with_lock(irq_lock, [=] {
        if (_state == state::armed) {
            _t._active_timers.erase(_t._active_timers.iterator_to(*this));
            cpu::current()->timers._list.erase(cpu::current()->timers._list.iterator_to(*this));
        }
        _state = state::free;
    });
    // even if we remove the first timer, allow it to expire rather than
    // reprogramming the timer
}

bool timer_base::expired() const
{
    return _state == state::expired;
}

bool operator<(const timer_base& t1, const timer_base& t2)
{
    if (t1._time < t2._time) {
        return true;
    } else if (t1._time == t2._time) {
        return &t1 < &t2;
    } else {
        return false;
    }
}

thread::reaper::reaper()
    : _mtx{}, _zombies{}, _thread([=] { reap(); })
{
    _thread.start();
}

void thread::reaper::reap()
{
    while (true) {
        with_lock(_mtx, [=] {
            wait_until(_mtx, [=] { return !_zombies.empty(); });
            while (!_zombies.empty()) {
                auto z = _zombies.front();
                _zombies.pop_front();
                z->join();
            }
        });
    }
}

void thread::reaper::add_zombie(thread* z)
{
    assert(z->_attr.detached);
    with_lock(_mtx, [=] {
        _zombies.push_back(z);
        _thread.wake();
    });
}

thread::reaper *thread::_s_reaper;

void init_detached_threads_reaper()
{
    thread::_s_reaper = new thread::reaper;
}

void init(elf::tls_data tls_data, std::function<void ()> cont)
{
    tls = tls_data;
    smp_init();
    thread::attr attr;
    attr.stack = { new char[4096*10], 4096*10 };
    attr.pinned_cpu = smp_initial_find_current_cpu();
    thread t{cont, attr, true};
    t.switch_to_first();
}

}
