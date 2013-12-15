/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef SCHED_HH_
#define SCHED_HH_

#include "arch.hh"
#include "arch-thread-state.hh"
#include "arch-cpu.hh"
#include <functional>
#include "tls.hh"
#include "drivers/clockevent.hh"
#include <boost/intrusive/set.hpp>
#include <boost/intrusive/list.hpp>
#include <osv/mutex.h>
#include <atomic>
#include "osv/lockless-queue.hh"
#include <list>
#include <memory>
#include <vector>
#include <osv/rcu.hh>

// If RUNTIME_PSEUDOFLOAT, runtime_t is a pseudofloat<>. Otherwise, a float.
#undef RUNTIME_PSEUDOFLOAT
#ifdef RUNTIME_PSEUDOFLOAT
#include <osv/pseudofloat.hh>
#else
typedef float runtime_t;
#endif

extern "C" {
void smp_main();
};
void smp_launch();

// Avoid #include "elf.hh", as it recursively includes sched.hh. We just
// need pointer to elf::tls_data
namespace elf {
    struct tls_data;
}

/**
 * OSV Scheduler namespace
 */
namespace sched {

class thread;
class thread_handle;
struct cpu;
class timer;
class timer_list;
class cpu_mask;
class thread_runtime_compare;

void schedule();

extern "C" {
    void thread_main_c(thread* t);
};

namespace bi = boost::intrusive;

const unsigned max_cpus = sizeof(unsigned long) * 8;

class cpu_set {
public:
    explicit cpu_set() : _mask() {}
    cpu_set(const cpu_set& other) : _mask(other._mask.load(std::memory_order_relaxed)) {}
    void set(unsigned c) {
        _mask.fetch_or(1UL << c, std::memory_order_release);
    }
    bool test_and_set(unsigned c) {
        unsigned bit = 1UL << c;
        return _mask.fetch_or(bit, std::memory_order_release) & bit;
    }
    bool test_all_and_set(unsigned c) {
        return _mask.fetch_or(1UL << c, std::memory_order_release);
    }
    void clear(unsigned c) {
        _mask.fetch_and(~(1UL << c), std::memory_order_release);
    }
    class iterator;
    iterator begin() {
        return iterator(*this);
    }
    iterator end() {
        return iterator(*this, max_cpus);
    }
    cpu_set fetch_clear() {
        cpu_set ret;
        if (_mask.load(std::memory_order_relaxed)) {
            ret._mask = _mask.exchange(0, std::memory_order_acquire);
        }
        return ret;
    }
    operator bool() const {
        return _mask.load(std::memory_order_relaxed);
    }
    class iterator {
    public:
        explicit iterator(cpu_set& set)
            : _set(set), _idx(0) {
            advance();
        }
        explicit iterator(cpu_set& set, unsigned idx)
            : _set(set), _idx(idx) {}
        iterator(const iterator& other)
            : _set(other._set), _idx(other._idx) {}
        iterator& operator=(const iterator& other) {
            if (this != &other) {
                this->~iterator();
                new (this) iterator(other);
            }
            return *this;
        }
        unsigned operator*() { return _idx; }
        iterator& operator++() {
            ++_idx;
            advance();
            return *this;
        }
        iterator operator++(int) {
            iterator tmp(*this);
            ++*this;
            return tmp;
        }
        bool operator==(const iterator& other) const {
            return _idx == other._idx;
        }
        bool operator!=(const iterator& other) const {
            return _idx != other._idx;
        }
    private:
        void advance() {
            unsigned long tmp = _set._mask.load(std::memory_order_relaxed);
            tmp &= ~((1UL << _idx) - 1);
            if (tmp) {
                _idx = __builtin_ctzl(tmp);
            } else {
                _idx = max_cpus;
            }
        }
    private:
        cpu_set& _set;
        unsigned _idx;
    };
private:
    std::atomic<unsigned long> _mask;
};

class timer_base : public bi::set_base_hook<>, public bi::list_base_hook<> {
public:
    class client {
    public:
        virtual ~client() {}
        virtual void timer_fired() = 0;
        void suspend_timers();
        void resume_timers();
    private:
        bool _timers_need_reload = false;
        bi::list<timer_base> _active_timers;
        friend class timer_base;
    };
public:
    explicit timer_base(client& t);
    ~timer_base();
    void set(s64 time);
    bool expired() const;
    void cancel();
    friend bool operator<(const timer_base& t1, const timer_base& t2);
private:
    void expire();
protected:
    client& _t;
    enum class state {
        free, armed, expired
    };
    state _state = state::free;
    s64 _time;
    friend class timer_list;
};

class timer : public timer_base {
public:
    explicit timer(thread& t);
};

// thread_runtime is used to maintain the scheduler's view of the thread's
// priority relative to other threads. It knows about a static priority of the
// thread (allowing a certain thread to get more runtime than another threads)
// and is used to maintain the "runtime" of each thread, a number which the
// scheduler uses to decide which thread to run next, and for how long.
// All methods of this class should be called only from within the scheduler.
// https://docs.google.com/document/d/1W7KCxOxP-1Fy5EyF2lbJGE2WuKmu5v0suYqoHas1jRM
class thread_runtime {
public:
    // Get the thread's CPU-local runtime, a number used to sort the runqueue
    // on this CPU (lowest runtime will be run first). local runtime cannot be
    // compared between different different CPUs - see export_runtime().
    inline runtime_t get_local() const
    {
        return _Rtt;
    }
    // Convert the thread's CPU-local runtime to a global scale which can be
    // understood on any CPU. Use this function when migrating the thread to a
    // different CPU, and the destination CPU should run update_after_sleep().
    void export_runtime();
    // Update the thread's local runtime after a sleep, when we potentially
    // missed one or more renormalization steps (which were only done to
    // runnable threads), or need to convert global runtime to local runtime.
    void update_after_sleep();
    // Increase thread's runtime considering that it has now run for "time"
    // nanoseconds at the current priority.
    // Remember that the run queue is ordered by local runtime, so never call
    // ran_for() or hysteresis_*() on a thread which is already in the queue.
    void ran_for(s64 time);
    // Temporarily decrease the running thread's runtime to provide hysteresis
    // (avoid switching threads quickly after deciding on one).
    // Use hystersis_run_start() when switching to a thread, and
    // hysteresis_run_stop() when switching away from a thread.
    void hysteresis_run_start();
    void hysteresis_run_stop();
    void add_context_switch_penalty();
    // Given a target local runtime higher than our own, calculate how much
    // time (in nanoseconds) it would take until ran_for(time) would bring our
    // thread to the given target. Returns -1 if the time is too long to
    // express in s64.
    s64 time_until(runtime_t target_local_runtime) const;

    void set_priority(runtime_t priority) {
        _priority = priority;
    }

    runtime_t priority() const {
        return _priority;
    }

    // set runtime from another thread's runtime. The other thread must
    // be on the same CPU's runqueue.
    void set_local(thread_runtime &other) {
        _Rtt = other._Rtt;
        _renormalize_count = other._renormalize_count;
    }

    // When _Rtt=0, multiplicative normalization doesn't matter, so it doesn't
    // matter what we set for _renormalize_count. We can't set it properly
    // in the constructor (it doesn't run from the scheduler, or know which
    // CPU's counter to copy), so we'll fix it in ran_for().
    constexpr thread_runtime(runtime_t priority) :
            _priority(priority),
            _Rtt(0), _renormalize_count(-1) { };

private:
    runtime_t _priority;            // p in the document
    runtime_t _Rtt;                 // R'' in the document
    // If _renormalize_count == -1, it means the runtime is global
    // (i.e., export_runtime() was called, or this is a new thread).
    int _renormalize_count;
};

/**
 * OSv thread
 */
class thread : private timer_base::client {
private:
    struct detached_state;
public:
    struct stack_info {
        stack_info();
        stack_info(void* begin, size_t size);
        void* begin;
        size_t size;
        void (*deleter)(stack_info si);  // null: don't delete
        static void default_deleter(stack_info si);
    };
    struct attr {
        stack_info stack;
        cpu *pinned_cpu;
        bool detached = false;
        attr(cpu *pinned_cpu = nullptr) : pinned_cpu(pinned_cpu) { }
    };

private:
    // Unlike the detached user visible attribute, those are internal to
    // our state machine. They exist to synchronize the detach() method against
    // thread completion and deletion.
    //
    // When a thread starts with _attr.detached = false, detach_state = attached.
    // When a thread starts with _attr.detached = true, detach_state == detached.
    //
    // Upon completion:
    //   if detach_state == attached, detach_state <= attached_complete. (atomically)
    //   This means that the thread was completedi while in attached state, and is waiting
    //   for someone to join or detach it.
    //
    // Upon detach:
    //   if detach_state == attached, detach_state <= detached.
    //   if detach_state == attached_complete, that means the detacher found a
    //   thread already complete. detach() being called means join() won't, so
    //   detach() is responsible for releasing its resources.
    enum class detach_state { attached, detached, attached_complete };
    std::atomic<detach_state> _detach_state = { detach_state::attached };

public:
    explicit thread(std::function<void ()> func, attr attributes = attr(),
            bool main = false);
    ~thread();
    void start();
    template <class Pred>
    static void wait_until(Pred pred);
    template <class Pred>
    static void wait_until(mutex_t& mtx, Pred pred);
    template <class Pred>
    static void wait_until(mutex_t* mtx, Pred pred);
    void wake();
    template <class Action>
    inline void wake_with(Action action);
    static void sleep_until(s64 abstime);
    static void yield();
    static void exit() __attribute__((__noreturn__));
    static thread* current() __attribute((no_instrument_function));
    stack_info get_stack_info();
    cpu* tcpu() const __attribute__((no_instrument_function));
    void join();
    void detach();
    void set_cleanup(std::function<void ()> cleanup);
    unsigned long id() __attribute__((no_instrument_function)); // guaranteed unique over system lifetime
    void* get_tls(ulong module);
    void* setup_tls(ulong module, const void* tls_template,
            size_t init_size, size_t uninit_size);
    /**
     * Set thread's priority
     *
     * Set the thread's priority, used to determine how much CPU time it will
     * get when competing for CPU time with other threads. The priority is a
     * floating-point number in (0,inf], with lower priority getting more
     * runtime. If one thread has priority s and a second has s/2, the second
     * thread will get twice as much runtime than the first.
     * An infinite priority (also sched::thread::priority_idle) means that the
     * thread will only get to run when no other wants to run.
     *
     * The default priority for new threads is sched::thread::priority_default
     * (1.0).
     */
    void set_priority(float priority);
    static constexpr float priority_idle = std::numeric_limits<float>::infinity();
    static constexpr float priority_default = 1.0;
    /**
     * Get thread's priority
     *
     * Returns the thread's priority, a floating-point number whose meaning is
     * explained in set_priority().
     */
    float priority() const;
private:
    static void wake_impl(detached_state* st);
    void main();
    void switch_to();
    void switch_to_first();
    void prepare_wait();
    void wait();
    void stop_wait();
    void init_stack();
    void setup_tcb();
    void free_tcb();
    void complete() __attribute__((__noreturn__));
    template <class Mutex, class Pred>
    static void do_wait_until(Mutex& mtx, Pred pred);
    struct dummy_lock {};
    friend void acquire(dummy_lock&) {}
    friend void release(dummy_lock&) {}
    friend void start_early_threads();
    template <typename T> T& remote_thread_local_var(T& var);
    void* do_remote_thread_local_var(void* var);
    thread_handle handle();
private:
    virtual void timer_fired() override;
    struct detached_state;
    friend struct detached_state;
private:
    std::function<void ()> _func;
    thread_state _state;
    thread_control_block* _tcb;
    enum class status {
        invalid,
        prestarted,
        unstarted,
        waiting,
        running,
        queued,
        waking, // between waiting and queued
        terminating, // temporary state used in complete()
        terminated,
    };
    thread_runtime _runtime;
    // part of the thread state is detached from the thread structure,
    // and freed by rcu, so that waking a thread and destroying it can
    // occur in parallel without synchronization via thread_handle
    struct detached_state {
        explicit detached_state(thread* t) : t(t) {}
        thread* t;
        cpu* _cpu;
        std::atomic<status> st = { status::unstarted };
    };
    std::unique_ptr<detached_state> _detached_state;
    attr _attr;
    arch_thread _arch;
    arch_fpu _fpu;
    unsigned int _id;
    std::function<void ()> _cleanup;
    std::vector<std::unique_ptr<char[]>> _tls;
    void destroy();
    friend class thread_ref_guard;
    friend void thread_main_c(thread* t);
    friend class wait_guard;
    friend struct cpu;
    friend class timer;
    friend class thread_runtime_compare;
    friend struct arch_cpu;
    friend class thread_runtime;
    friend void ::smp_main();
    friend void ::smp_launch();
    friend void init(std::function<void ()> cont);
public:
    thread* _joiner;
    bi::set_member_hook<> _runqueue_link;
    // see cpu class
    lockless_queue_link<thread> _wakeup_link;
    static unsigned long _s_idgen;
    static thread *find_by_id(unsigned int id);
private:
    class reaper;
    friend class reaper;
    friend class thread_handle;
    static reaper* _s_reaper;
    friend void init_detached_threads_reaper();
};

class thread_handle {
public:
    thread_handle() = default;
    thread_handle(const thread_handle& t) { _t.assign(t._t.read()); }
    thread_handle(thread& t) { reset(t); }
    void reset(thread& t) { _t.assign(t._detached_state.get()); }
    void wake();
    void clear() { _t.assign(nullptr); }
private:
    osv::rcu_ptr<thread::detached_state> _t;
};

void init_detached_threads_reaper();

class timer_list {
public:
    void fired();
    void suspend(bi::list<timer_base>& t);
    void resume(bi::list<timer_base>& t);
    void rearm();
private:
    friend class timer_base;
    s64 _last = std::numeric_limits<s64>::max();
    bi::set<timer_base, bi::base_hook<bi::set_base_hook<>>> _list;
    class callback_dispatch : private clock_event_callback {
    public:
        callback_dispatch();
        virtual void fired();
    };
    static callback_dispatch _dispatch;
};

class thread_runtime_compare {
public:
    bool operator()(const thread& t1, const thread& t2) const {
        return t1._runtime.get_local() < t2._runtime.get_local();
    }
};

typedef bi::rbtree<thread,
                   bi::member_hook<thread,
                                   bi::set_member_hook<>,
                                   &thread::_runqueue_link>,
                   bi::compare<thread_runtime_compare>,
                   bi::constant_time_size<true> // for load estimation
                  > runqueue_type;

struct cpu : private timer_base::client {
    explicit cpu(unsigned id);
    unsigned id;
    struct arch_cpu arch;
    thread* bringup_thread;
    runqueue_type runqueue;
    timer_list timers;
    timer_base preemption_timer;
    thread* idle_thread;
    // if true, cpu is now polling incoming_wakeups_mask
    std::atomic<bool> idle_poll = { false };
    // for each cpu, a list of threads that are migrating into this cpu:
    typedef lockless_queue<thread, &thread::_wakeup_link> incoming_wakeup_queue;
    cpu_set incoming_wakeups_mask;
    incoming_wakeup_queue* incoming_wakeups;
    thread* terminating_thread;
    s64 running_since;
    char* percpu_base;
    static cpu* current();
    void init_on_cpu();
    void schedule();
    void handle_incoming_wakeups();
    bool poll_wakeup_queue();
    void idle();
    void do_idle();
    void idle_poll_start();
    void idle_poll_end();
    void send_wakeup_ipi();
    void load_balance();
    unsigned load();
    void reschedule_from_interrupt(bool preempt = false);
    void enqueue(thread& t);
    void init_idle_thread();
    virtual void timer_fired() override;
    class notifier;
    // For scheduler:
    runtime_t c;
    int renormalize_count;
};

class cpu::notifier {
public:
    explicit notifier(std::function<void ()> cpu_up);
    ~notifier();
private:
    static void fire();
private:
    std::function<void ()> _cpu_up;
    static mutex _mtx;
    static std::list<notifier*> _notifiers;
    friend struct cpu;
};

void preempt();
void preempt_disable() __attribute__((no_instrument_function));
void preempt_enable() __attribute__((no_instrument_function));
bool preemptable() __attribute__((no_instrument_function));

thread* current();

class wait_guard {
public:
    wait_guard(thread* t) : _t(t) { t->prepare_wait(); }
    ~wait_guard() { _t->stop_wait(); }
private:
    thread* _t;
};

// does not return - continues to @cont instead
void init(std::function<void ()> cont);

void init_tls(elf::tls_data tls);

inline void acquire(mutex_t& mtx)
{
    mutex_lock(&mtx);
}

inline void release(mutex_t& mtx)
{
    mutex_unlock(&mtx);
}

inline void acquire(mutex_t* mtx)
{
    if (mtx) {
        mutex_lock(mtx);
    }
}

inline void release(mutex_t* mtx)
{
    if (mtx) {
        mutex_unlock(mtx);
    }
}

template <class Mutex, class Pred>
inline
void thread::do_wait_until(Mutex& mtx, Pred pred)
{
    assert(arch::irq_enabled());
    assert(preemptable());

    thread* me = current();
    while (true) {
        {
            wait_guard waiter(me);
            if (pred()) {
                return;
            }
            release(mtx);
            me->wait();
        }
        acquire(mtx);
    }
}

template <class Pred>
inline
void thread::wait_until(Pred pred)
{
    dummy_lock mtx;
    do_wait_until(mtx, pred);
}

template <class Pred>
inline
void thread::wait_until(mutex_t& mtx, Pred pred)
{
    do_wait_until(mtx, pred);
}

template <class Pred>
inline
void thread::wait_until(mutex_t* mtx, Pred pred)
{
    do_wait_until(mtx, pred);
}

// About wake_with():
//
// Consider one thread doing:
//     wait_until([&] { return *x == 0; })
// the almost-correct way to wake it is:
//     *x = 0;
//     t->wake();
// This is only "almost" correct, because doing *x = 0 may already cause the
// thread to wake up (if it wasn't sleeping yet, or because of a spurious
// wakeup) and may decide to exit, in which case t->wake() may crash. So to be
// safe we need to use rcu, which prevents a thread's detached_state destruction
// while in an rcu read-side critical section
//     thread_handle h(t.handle());
//     *x = 0;
//     h.wake();  // uses rcu to prevent concurrent detached state destruction
// wake_with is a convenient one-line shortcut for the above three lines,
// with a syntax mirroring that of wait_until():
//     t->wake_with([&] { *x = 0; });

template <class Action>
inline
void thread::wake_with(Action action)
{
    WITH_LOCK(osv::rcu_read_lock) {
        auto ds = _detached_state.get();
        action();
        wake_impl(ds);
    }
}

extern cpu __thread* current_cpu;

inline cpu* thread::tcpu() const
{
    return _detached_state->_cpu;
}

inline thread_handle thread::handle()
{
    return thread_handle(*this);
}

inline cpu* cpu::current()
{
    return current_cpu;
}

inline
timer::timer(thread& t)
    : timer_base(t)
{
}

extern std::vector<cpu*> cpus;

}

#endif /* SCHED_HH_ */
