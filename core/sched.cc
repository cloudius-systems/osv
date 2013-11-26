/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

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
#include "prio.hh"
#include "elf.hh"
#include <stdlib.h>

__thread char* percpu_base;

extern char _percpu_start[], _percpu_end[];

namespace sched {

TRACEPOINT(trace_sched_switch, "to %p vold=%g vnew=%g", thread*, float, float);
TRACEPOINT(trace_sched_wait, "");
TRACEPOINT(trace_sched_wake, "wake %p", thread*);
TRACEPOINT(trace_sched_migrate, "thread=%p cpu=%d", thread*, unsigned);
TRACEPOINT(trace_sched_queue, "thread=%p", thread*);
TRACEPOINT(trace_sched_preempt, "");
TRACEPOINT(trace_timer_set, "timer=%p time=%d", timer_base*, s64);
TRACEPOINT(trace_timer_cancel, "timer=%p", timer_base*);
TRACEPOINT(trace_timer_fired, "timer=%p", timer_base*);

std::vector<cpu*> cpus __attribute__((init_priority((int)init_prio::cpus)));

thread __thread * s_current;
cpu __thread * current_cpu;

unsigned __thread preempt_counter = 1;
bool __thread need_reschedule = false;

elf::tls_data tls;

inter_processor_interrupt wakeup_ipi{[] {}};

// "tau" controls the length of the history we consider for scheduling,
// or more accurately the rate of decay of an exponential moving average.
// In particular, it can be seen that if a thread has been monopolizing the
// CPU, and a long-sleeping thread wakes up (or new thread is created),
// the new thread will get to run for ln2*tau. (ln2 is roughly 0.7).
constexpr s64 tau = 200_ms;

// "thyst" controls the hysteresis algorithm which temporarily gives a
// running thread some extra runtime before preempting it. We subtract thyst
// when the thread is switched in, and add it back when the thread is switched
// out. In particular, it can be shown that when two cpu-busy threads at equal
// priority compete, they will alternate at time-slices of 2*thyst; Also,
// the distance between two preemption interrupts cannot be lower than thyst.
constexpr s64 thyst = 2_ms;

constexpr s64 context_switch_penalty = 10_us;

constexpr float cmax = 0x1P63;
constexpr float cinitial = 0x1P-63;

static inline float exp_tau(s64 t) {
    // return expf((float)t/(float)tau);
    // Approximate e^x as much faster 1+x for x<0.001 (the error is O(x^2)).
    // Further speed up by comparing and adding integers as much as we can:
    static constexpr int m = tau / 1000;
    static constexpr float invtau = 1.0f / tau;
    if (t < m && t > -m)
        return (tau + t) * invtau;
    else
        return expf(t * invtau);
}

// fastlog2() is an approximation of log2, designed for speed over accuracy
// (it is accurate to roughly 5 digits).
// The function is copyright (C) 2012 Paul Mineiro, released under the
// BSD license. See https://code.google.com/p/fastapprox/.
static inline float
fastlog2 (float x)
{
    union { float f; u32 i; } vx = { x };
    union { u32 i; float f; } mx = { (vx.i & 0x007FFFFF) | 0x3f000000 };
    float y = vx.i;
    y *= 1.1920928955078125e-7f;
    return y - 124.22551499f - 1.498030302f * mx.f
            - 1.72587999f / (0.3520887068f + mx.f);
}

static inline float taulog(float f) {
    //return tau * logf(f);
    // We don't need the full accuracy of logf - we use this in time_until(),
    // where it's fine to overshoot, even significantly, the correct time
    // because a thread running a bit too much will "pay" in runtime.
    // We multiply by 1.01 to ensure overshoot, not undershoot.
    static constexpr float tau2 = tau * 0.69314718f * 1.01;
    return tau2 * fastlog2(f);
}

static constexpr runtime_t inf = std::numeric_limits<runtime_t>::infinity();

mutex cpu::notifier::_mtx;
std::list<cpu::notifier*> cpu::notifier::_notifiers __attribute__((init_priority((int)init_prio::notifiers)));

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
    , c(cinitial)
    , renormalize_count(0)
{
    auto pcpu_size = _percpu_end - _percpu_start;
    // We want the want the per-cpu area to be aligned as the most strictly
    // aligned per-cpu variable. This is probably CACHELINE_ALIGNED (64 bytes)
    // but we'll be even stricter, and go for page (4096 bytes) alignment.
    percpu_base = (char *) aligned_alloc(4096, pcpu_size);
    memcpy(percpu_base, _percpu_start, pcpu_size);
    percpu_base -= reinterpret_cast<size_t>(_percpu_start);
    if (id == 0) {
        ::percpu_base = percpu_base;
    }
}

void cpu::init_idle_thread()
{
    idle_thread = new thread([this] { idle(); }, thread::attr(this));
    idle_thread->set_priority(thread::priority_idle);
}

void cpu::schedule()
{
    WITH_LOCK(irq_lock) {
        reschedule_from_interrupt();
    }
}

// In the x86 ABI, the FPU state is callee-saved, meaning that a program must
// not call a function in the middle of an FPU calculation. But if we get a
// preemption, i.e., the scheduler is called by an interrupt, the currently
// running thread might be in the middle of a floating point calculation,
// which we must not trample over.
// When the scheduler code itself doesn't use the FPU (!scheduler_uses_fpu),
// we need to save the running thread's FPU state only before switching to
// a different thread (and restore this thread's FPU state when coming back
// to this thread). However, if the scheduler itself uses the FPU in its
// calculations (scheduler_uses_fpu), we always (when in preemption) need
// to save and restore this thread's FPU state when we enter and exit the
// scheduler.
#ifdef RUNTIME_PSEUDOFLOAT
static constexpr bool scheduler_uses_fpu = false;
#else
static constexpr bool scheduler_uses_fpu = true;
#endif

void cpu::reschedule_from_interrupt(bool preempt)
{
    if (scheduler_uses_fpu && preempt) {
        thread::current()->_fpu.save();
    }

    need_reschedule = false;
    handle_incoming_wakeups();
    auto now = clock::get()->time();

    auto interval = now - running_since;
    running_since = now;
    if (interval == 0) {
        // During startup, the clock may be stuck and we get zero intervals.
        // To avoid scheduler loops, let's make it non-zero.
        interval = context_switch_penalty;
    }
    thread* p = thread::current();

    const auto p_status = p->_status.load();
    assert(p_status != thread::status::queued);

    p->_runtime.ran_for(interval);

    if (p_status == thread::status::running) {
        // The current thread is still runnable. Check if it still has the
        // lowest runtime, and update the timer until the next thread's turn.
        if (runqueue.empty()) {
            preemption_timer.cancel();
            if (scheduler_uses_fpu && preempt) {
                p->_fpu.restore();
            }
            return;
        } else {
            auto &t = *runqueue.begin();
            if (p->_runtime.get_local() < t._runtime.get_local()) {
                preemption_timer.cancel();
                auto delta = p->_runtime.time_until(t._runtime.get_local());
                if (delta > 0) {
                    preemption_timer.set(now + delta);
                }
                if (scheduler_uses_fpu && preempt) {
                    p->_fpu.restore();
                }
                return;
            }
        }
        // If we're here, p no longer has the lowest runtime. Before queuing
        // p, return the runtime it borrowed for hysteresis.
        p->_runtime.hysteresis_run_stop();
        p->_status.store(thread::status::queued);
        enqueue(*p);
    } else {
        // p is no longer running, so we'll switch to a different thread.
        // Return the runtime p borrowed for hysteresis.
        p->_runtime.hysteresis_run_stop();
    }

    auto ni = runqueue.begin();
    auto n = &*ni;
    runqueue.erase(ni);
    assert(n->_status.load() == thread::status::queued);
    trace_sched_switch(n, p->_runtime.get_local(), n->_runtime.get_local());
    n->_status.store(thread::status::running);
    n->_runtime.hysteresis_run_start();

    assert(n!=p);

    if (preempt) {
        trace_sched_preempt();
        if (!scheduler_uses_fpu) {
            // If runtime is not a float, we only need to save the FPU here,
            // just when deciding to switch threads.
            p->_fpu.save();
        }
    }
    if (p->_status.load(std::memory_order_relaxed) == thread::status::queued
            && p != idle_thread) {
        n->_runtime.add_context_switch_penalty();
    }
    preemption_timer.cancel();
    if (!runqueue.empty()) {
        auto& t = *runqueue.begin();
        auto delta = n->_runtime.time_until(t._runtime.get_local());
        if (delta > 0) {
            preemption_timer.set(now + delta);
        }
    }
    n->switch_to();
    if (p->_cpu->terminating_thread) {
        p->_cpu->terminating_thread->unref();
        p->_cpu->terminating_thread = nullptr;
    }

    if (preempt) {
        p->_fpu.restore();
    }
}

void cpu::timer_fired()
{
    // nothing to do, preemption will happen if needed
}

struct idle_poll_lock_type {
    explicit idle_poll_lock_type(cpu& c) : _c(c) {}
    void lock() { _c.idle_poll_start(); }
    void unlock() { _c.idle_poll_end(); }
    cpu& _c;
};

void cpu::idle_poll_start()
{
    idle_poll.store(true, std::memory_order_relaxed);
}

void cpu::idle_poll_end()
{
    idle_poll.store(false, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_seq_cst);
}

void cpu::send_wakeup_ipi()
{
    std::atomic_thread_fence(std::memory_order_seq_cst);
    if (!idle_poll.load(std::memory_order_relaxed)) {
        wakeup_ipi.send(this);
    }
}

void cpu::do_idle()
{
    do {
        idle_poll_lock_type idle_poll_lock{*this};
        WITH_LOCK(idle_poll_lock) {
            // spin for a bit before halting
            for (unsigned ctr = 0; ctr < 10000; ++ctr) {
                // FIXME: can we pull threads from loaded cpus?
                handle_incoming_wakeups();
                if (!runqueue.empty()) {
                    return;
                }
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

void start_early_threads();

void cpu::idle()
{
    if (id == 0) {
        start_early_threads();
    }

    while (true) {
        do_idle();
        // We have idle priority, so this runs the thread on the runqueue:
        schedule();
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
            WITH_LOCK(irq_lock) {
                if (&t == thread::current()) {
                    // Special case of current thread being woken before
                    // having a chance to be scheduled out.
                    t._status.store(thread::status::running);
                } else {
                    t._status.store(thread::status::queued);
                    // Make sure the CPU-local runtime measure is suitably
                    // normalized. We may need to convert a global value to the
                    // local value when waking up after a CPU migration, or to
                    // perform renormalizations which we missed while sleeping.
                    t._runtime.update_after_sleep();
                    enqueue(t);
                    t.resume_timers();
                }
            }
        }
    }
}

void cpu::enqueue(thread& t)
{
    trace_sched_queue(&t);
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
        // This CPU is temporarily running one extra thread (this thread),
        // so don't migrate a thread away if the difference is only 1.
        if (min->load() >= (load() - 1)) {
            continue;
        }
        WITH_LOCK(irq_lock) {
            auto i = std::find_if(runqueue.rbegin(), runqueue.rend(),
                    [](thread& t) { return !t._attr.pinned_cpu; });
            if (i == runqueue.rend()) {
                continue;
            }
            auto& mig = *i;
            trace_sched_migrate(&mig, min->id);
            runqueue.erase(std::prev(i.base()));  // i.base() returns off-by-one
            // we won't race with wake(), since we're not thread::waiting
            assert(mig._status.load() == thread::status::queued);
            mig._status.store(thread::status::waking);
            mig.suspend_timers();
            mig._cpu = min;
            // Convert the CPU-local runtime measure to a globally meaningful
            // measure
            mig._runtime.export_runtime();
            mig.remote_thread_local_var(::percpu_base) = min->percpu_base;
            mig.remote_thread_local_var(current_cpu) = min;
            min->incoming_wakeups[id].push_front(mig);
            min->incoming_wakeups_mask.set(id);
            // FIXME: avoid if the cpu is alive and if the priority does not
            // FIXME: warrant an interruption
            min->send_wakeup_ipi();
        }
    }
}

cpu::notifier::notifier(std::function<void ()> cpu_up)
    : _cpu_up(cpu_up)
{
    WITH_LOCK(_mtx) {
        _notifiers.push_back(this);
    }
}

cpu::notifier::~notifier()
{
    WITH_LOCK(_mtx) {
        _notifiers.remove(this);
    }
}

void cpu::notifier::fire()
{
    WITH_LOCK(_mtx) {
        for (auto n : _notifiers) {
            n->_cpu_up();
        }
    }
}

void schedule()
{
    cpu::current()->schedule();
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
    assert(t->_status.load() == status::running);
    // Do not yield to a thread with idle priority
    thread &tnext = *(t->_cpu->runqueue.begin());
    if (tnext.priority() == thread::priority_idle) {
        return;
    }
    t->_runtime.set_local(tnext._runtime);
    // Note that reschedule_from_interrupt will further increase t->_runtime
    // by thyst, giving the other thread 2*thyst to run before going back to t
    t->_cpu->reschedule_from_interrupt(false);
}

void thread::set_priority(float priority)
{
    _runtime.set_priority(priority);
}

float thread::priority() const
{
    return _runtime.priority();
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
thread_list_type thread_list __attribute__((init_priority((int)init_prio::threadlist)));
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
    , _runtime(thread::priority_default)
    , _attr(attr)
    , _ref_counter(1)
    , _joiner()
{
    WITH_LOCK(thread_list_mutex) {
        thread_list.push_back(*this);
        _id = _s_idgen++;
    }
    setup_tcb();
    // setup s_current before switching to the thread, so interrupts
    // can call thread::current()
    // remote_thread_local_var() doesn't work when there is no current
    // thread, so don't do this for main threads (switch_to_first will
    // do that for us instead)
    if (!main && sched::s_current) {
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
        if (_cpu == sched::cpus[0]) {
            s_current = this;
        }
        remote_thread_local_var(current_cpu) = _cpu;
    }
}

thread::~thread()
{
    if (!_attr.detached) {
        join();
    }
    WITH_LOCK(thread_list_mutex) {
        thread_list.erase(thread_list.iterator_to(*this));
    }
    if (_attr.stack.deleter) {
        _attr.stack.deleter(_attr.stack);
    }
    free_tcb();
}

void thread::start()
{
    assert(_status == status::unstarted);

    if (!sched::s_current) {
        _status.store(status::prestarted);
        return;
    }

    _cpu = _attr.pinned_cpu ? _attr.pinned_cpu : current()->tcpu();
    remote_thread_local_var(percpu_base) = _cpu->percpu_base;
    remote_thread_local_var(current_cpu) = _cpu;
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
    trace_sched_wake(this);
    status old_status = status::waiting;
    if (!_status.compare_exchange_strong(old_status, status::waking)) {
        return;
    }
    preempt_disable();
    unsigned c = cpu::current()->id;
    _cpu->incoming_wakeups[c].push_front(*this);
    if (!_cpu->incoming_wakeups_mask.test_all_and_set(c)) {
        // FIXME: avoid if the cpu is alive and if the priority does not
        // FIXME: warrant an interruption
        if (_cpu != current()->tcpu()) {
            _cpu->send_wakeup_ipi();
        } else {
            need_reschedule = true;
        }
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
    trace_sched_wait();
    schedule();
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
        schedule();
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

void* thread::get_tls(ulong module)
{
    if (module >= _tls.size()) {
        return nullptr;
    }
    return _tls[module].get();
}

void* thread::setup_tls(ulong module, const void* tls_template,
        size_t init_size, size_t uninit_size)
{
    _tls.resize(std::max(module + 1, _tls.size()));
    _tls[module].reset(new char[init_size + uninit_size]);
    auto p = _tls[module].get();
    memcpy(p, tls_template, init_size);
    memset(p + init_size, 0, uninit_size);
    return p;
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
    _last = std::numeric_limits<s64>::max();
    // don't hold iterators across list iteration, since the list can change
    while (!_list.empty() && _list.begin()->_time <= now) {
        auto j = _list.begin();
        j->expire();
        // timer_base::expire may have re-added j to the timer_list
        if (j->_state != timer_base::state::armed) {
            _list.erase(j);
        }
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
    WITH_LOCK(irq_lock) {
        auto& timers = cpu::current()->timers;
        timers._list.insert(*this);
        _t._active_timers.push_back(*this);
        if (timers._list.iterator_to(*this) == timers._list.begin()) {
            timers.rearm();
        }
    }
};

void timer_base::cancel()
{
    if (_state == state::free) {
        return;
    }
    trace_timer_cancel(this);
    irq_save_lock_type irq_lock;
    WITH_LOCK(irq_lock) {
        if (_state == state::armed) {
            _t._active_timers.erase(_t._active_timers.iterator_to(*this));
            cpu::current()->timers._list.erase(cpu::current()->timers._list.iterator_to(*this));
        }
        _state = state::free;
    }
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
        WITH_LOCK(_mtx) {
            wait_until(_mtx, [=] { return !_zombies.empty(); });
            while (!_zombies.empty()) {
                auto z = _zombies.front();
                _zombies.pop_front();
                z->join();
            }
        }
    }
}

void thread::reaper::add_zombie(thread* z)
{
    assert(z->_attr.detached);
    WITH_LOCK(_mtx) {
        _zombies.push_back(z);
        _thread.wake();
    }
}

thread::reaper *thread::_s_reaper;

void init_detached_threads_reaper()
{
    thread::_s_reaper = new thread::reaper;
}

void start_early_threads()
{
    WITH_LOCK(thread_list_mutex) {
        for (auto& t : thread_list) {
            if (&t == sched::thread::current()) {
                continue;
            }
            t.remote_thread_local_var(s_current) = &t;
            thread::status expected = thread::status::prestarted;
            if (t._status.compare_exchange_strong(expected,
                thread::status::unstarted, std::memory_order_relaxed)) {
                t.start();
            }
        }
    }
}

void init(std::function<void ()> cont)
{
    thread::attr attr;
    attr.stack = { new char[4096*10], 4096*10 };
    attr.pinned_cpu = smp_initial_find_current_cpu();
    thread t{cont, attr, true};
    t.switch_to_first();
}

void init_tls(elf::tls_data tls_data)
{
    tls = tls_data;
}

// For a description of the algorithms behind the thread_runtime::*
// implementation, please refer to:
// https://docs.google.com/document/d/1W7KCxOxP-1Fy5EyF2lbJGE2WuKmu5v0suYqoHas1jRM

void thread_runtime::export_runtime()
{
    _Rtt /= cpu::current()->c;;
    _renormalize_count = -1; // special signal to update_after_sleep()
}

void thread_runtime::update_after_sleep()
{
    auto cpu_renormalize_count = cpu::current()->renormalize_count;
    if (_renormalize_count == cpu_renormalize_count) {
        return;
    }
    if (_renormalize_count == -1) {
        // export_runtime() was used to convert the CPU-local runtime to
        // a global value. We need to convert it back to a local value,
        // suitable for this CPU.
        _Rtt *= cpu::current()->c;
    } else if (_renormalize_count + 1 == cpu_renormalize_count) {
        _Rtt *= cinitial / cmax;
    } else if (_Rtt != inf) {
        // We need to divide by cmax^2 or even a higher power. We assume
        // this will bring Rtt to zero anyway, so no sense in doing an
        // accurate calculation
        _Rtt = 0;
    }
    _renormalize_count = cpu_renormalize_count;
}

void thread_runtime::ran_for(s64 time)
{
    assert (_priority > 0);
    assert (time >= 0);

    cpu *curcpu = cpu::current();

    // When a thread is created, it gets _Rtt = 0, so its _renormalize_count
    // is irrelevant, and couldn't be set correctly in the constructor.
    // So set it here.
    if (!_Rtt) {
        _renormalize_count = curcpu->renormalize_count;
    }

    const auto cold = curcpu->c;
    const auto cnew = cold * exp_tau(time);

    // During our boot process, unfortunately clock::time() jumps by the
    // amount of host uptime, which can be huge and cause the above
    // calculation to overflow. In that case, just ignore this time period.
    if (cnew == inf) {
        return;
    }
    curcpu->c = cnew;

    _Rtt += _priority * (cnew - cold);

    assert (_renormalize_count != -1); // forgot to update_after_sleep?

    // As time goes by, the normalization constant c grows towards infinity.
    // To avoid an overflow, we need to renormalize if c becomes too big.
    // We only renormalize the runtime of the running or runnable threads.
    // Sleeping threads will be renormalized when they wake
    // (see update_after_sleep()), depending on the number of renormalization
    // steps they have missed (this is why we need to keep a counter).
    if (cnew < cmax) {
        return;
    }
    if (++curcpu->renormalize_count < 0) {
        // Don't use negative values (We use -1 to mark export_runtime())
        curcpu->renormalize_count = 0;
    }
    _Rtt *= cinitial / cmax;
    _renormalize_count = curcpu->renormalize_count;
    for (auto &t : curcpu->runqueue) {
        if (t._runtime._renormalize_count >= 0) {
            t._runtime._Rtt *= cinitial / cmax;
            t._runtime._renormalize_count++;
        }
    }
    curcpu->c *= cinitial / cmax;
}

const auto hysteresis_mul_exp_tau = exp_tau(thyst);
const auto hysteresis_div_exp_tau = exp_tau(-thyst);
const auto penalty_exp_tau = exp_tau(context_switch_penalty);

void thread_runtime::hysteresis_run_start()
{
    // Optimized version of ran_for(-thyst);
    if (!_Rtt) {
        _renormalize_count = cpu::current()->renormalize_count;
    }
    const auto cold = cpu::current()->c;
    const auto cnew = cold * hysteresis_div_exp_tau;
    cpu::current()->c = cnew;
    if (_priority == inf) {
        // TODO: the only reason we need this case is so that time<0
        // will bring us to +inf, not -inf. Think if there's a cleaner
        // alternative to doing this if.
        _Rtt = inf;
    } else {
        _Rtt += _priority * (cnew - cold);
    }
}

void thread_runtime::hysteresis_run_stop()
{
    // Optimized version of ran_for(thyst);
    if (!_Rtt) {
        _renormalize_count = cpu::current()->renormalize_count;
    }
    const auto cold = cpu::current()->c;
    const auto cnew = cold * hysteresis_mul_exp_tau;
    cpu::current()->c = cnew;
    _Rtt += _priority * (cnew - cold);
}

void thread_runtime::add_context_switch_penalty()
{
    // Does the same as: ran_for(context_switch_penalty);
    const auto cold = cpu::current()->c;
    const auto cnew = cold * penalty_exp_tau;
    cpu::current()->c = cnew;
    _Rtt += _priority * (cnew - cold);

}

s64 thread_runtime::time_until(runtime_t target_local_runtime) const
{
    if (_priority == inf) {
        return -1;
    }
    if (target_local_runtime == inf) {
        return -1;
    }
    auto ret = taulog(runtime_t(1) +
            (target_local_runtime - _Rtt) / _priority / cpu::current()->c);
    if (ret > (runtime_t)std::numeric_limits<s64>::max())
        return -1;
    return (s64) ret;
}

}

irq_lock_type irq_lock;
