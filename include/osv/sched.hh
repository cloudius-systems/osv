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
#include <osv/tls.hh>
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
#include <osv/clock.hh>

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

// Avoid #include <osv/elf.hh>, as it recursively includes sched.hh. We just
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
template <typename T> class wait_object;

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
            ret._mask.store(_mask.exchange(0, std::memory_order_acquire),
                            std::memory_order_relaxed);
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
            if (++_idx < max_cpus) {
                advance();
            }
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
    void set(osv::clock::uptime::time_point time);
    void reset(osv::clock::uptime::time_point time);
    // Set a timer using absolute wall-clock time.
    // CAVEAT EMPTOR: Internally timers are kept using the monotonic (uptime)
    // clock, so the wall-time given here is converted to an uptime.
    // This basically means that the duration until the timer's expiration is
    // fixed on the call to set(), even if the wall clock is later adjusted.
    // When the timer expires, the current wall-time may not be identical to
    // the intended expiration wall-time.
    void set(osv::clock::wall::time_point time) {
        set(osv::clock::uptime::time_point(
                time - osv::clock::wall::boot_time()));
    }
    // Duration (time relative to now) version of set():
    template <class Rep, class Period>
    void set(std::chrono::duration<Rep, Period> duration) {
        set(osv::clock::uptime::now() + duration);
    }
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
    osv::clock::uptime::time_point _time;
    friend class timer_list;
};

class timer : public timer_base {
public:
    explicit timer(thread& t);
private:
    class waiter;
    friend class wait_object<timer>;
};

template <>
class wait_object<timer> {
public:
    explicit wait_object(timer& tmr, mutex* mtx = nullptr) : _tmr(tmr) {}
    bool poll() const { return _tmr.expired(); }
    void arm() {}
    void disarm() {}
private:
    timer& _tmr;
};

extern thread __thread * s_current;
// thread_runtime is used to maintain the scheduler's view of the thread's
// priority relative to other threads. It knows about a static priority of the
// thread (allowing a certain thread to get more runtime than another threads)
// and is used to maintain the "runtime" of each thread, a number which the
// scheduler uses to decide which thread to run next, and for how long.
// All methods of this class should be called only from within the scheduler.
// https://docs.google.com/document/d/1W7KCxOxP-1Fy5EyF2lbJGE2WuKmu5v0suYqoHas1jRM
class thread_runtime {
public:
    using duration = osv::clock::uptime::duration;
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
    void ran_for(duration time);
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
    duration time_until(runtime_t target_local_runtime) const;

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
        stack_info _stack;
        cpu *_pinned_cpu;
        bool _detached;
        std::array<char, 16> _name = {};
        attr() : _pinned_cpu(nullptr), _detached(false) { }
        attr &pin(cpu *c) {
            _pinned_cpu = c;
            return *this;
        }
        attr &stack(size_t stacksize) {
            _stack = stack_info(nullptr, stacksize);
            return *this;
        }
        attr &stack(const stack_info &s) {
            _stack = s;
            return *this;
        }
        attr &detached(bool val = true) {
            _detached = val;
            return *this;
        }
        attr& name(std::string n) {
            strncpy(_name.data(), n.data(), sizeof(_name) - 1);
            return *this;
        }
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
    static void wait_until_interruptible(Pred pred);
    template <class Pred>
    static void wait_until(Pred pred);
    template <class Pred>
    static void wait_until(mutex_t& mtx, Pred pred);
    template <class Pred>
    static void wait_until(mutex_t* mtx, Pred pred);

    // Wait for any of a number of waitable objects to be signalled
    // waitable objects include: waitqueues, timers, predicates,
    // and more.  If supplied, the mutex object is unlocked while waiting.
    template <typename... waitable>
    static void wait_for(waitable&&... waitables);
    template <typename... waitable>
    static void wait_for(mutex& mtx, waitable&&... waitables);

    void wake();
    cpu* get_cpu() const {
        return _detached_state.get()->_cpu;
    }
    // wake up after acquiring mtx
    //
    // mtx must be locked, and wr must be a free wait_record that will
    // survive until the thread wakes up.  wake_lock() will not cause
    // the thread to wake up immediately; only when it becomes possible
    // for it to take the mutex.
    void wake_lock(mutex* mtx, wait_record* wr);
    bool interrupted();
    void interrupted(bool f);
    template <class Action>
    inline void wake_with(Action action);
    // for mutex internal use
    template <class Action>
    inline void wake_with_from_mutex(Action action);
    template <class Rep, class Period>
    static void sleep(std::chrono::duration<Rep, Period> duration);
    static void yield();
    static void exit() __attribute__((__noreturn__));
#ifdef __OSV_CORE__
    static inline thread* current() { return s_current; };
#else
    static thread* current();
#endif
    stack_info get_stack_info();
    cpu* tcpu() const __attribute__((no_instrument_function));
    void join();
    void detach();
    void set_cleanup(std::function<void ()> cleanup);
    /**
     * Return thread's numeric id
     *
     * In OSv, threads are sched::thread objects and are usually referred to
     * with a pointer, not a numeric id. Nevertheless, for Linux compatibility
     * it is convenient for each thread to have a numeric id which emulates
     * Linux's thread ids. The id() function returns this thread id.
     *
     * id() returns the same value for the life-time of the thread. A thread's
     * id is guaranteed to be unique among currently running threads, but may
     * not be unique for the lifetime of the system: Thread ids are assigned
     * sequentially to new threads (skipping ids which are currently in use),
     * and this sequential 32-bit counter can wrap around.
     */
    unsigned int id() __attribute__((no_instrument_function));
    void* get_tls(ulong module);
    void* setup_tls(ulong module, const void* tls_template,
            size_t init_size, size_t uninit_size);
    void set_name(std::string name);
    std::string name() const;
    std::array<char, 16> name_raw() const { return _attr._name; }
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
    static void wake_impl(detached_state* st,
            unsigned allowed_initial_states_mask = 1 << unsigned(status::waiting));
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
    template <class Action>
    inline void do_wake_with(Action action, unsigned allowed_initial_states_mask);
    template <class IntrStrategy, class Mutex, class Pred>
    static void do_wait_until(Mutex& mtx, Pred pred);
    template <typename Mutex, typename... wait_object>
    static void do_wait_for(Mutex& mtx, wait_object&&... wait_objects);
    struct dummy_lock { void receive_lock() {} };
    friend void acquire(dummy_lock&) {}
    friend void release(dummy_lock&) {}
    friend void start_early_threads();
    void* do_remote_thread_local_var(void* var);
    thread_handle handle();
public:
    template <typename T>
    T& remote_thread_local_var(T& var)
    {
        return *static_cast<T*>(do_remote_thread_local_var(&var));
    }

    template <typename T>
    T* remote_thread_local_ptr(void* var)
    {
        return static_cast<T*>(do_remote_thread_local_var(var));
    }
private:
    virtual void timer_fired() override;
    struct detached_state;
    friend struct detached_state;
private:
    std::function<void ()> _func;
    thread_state _state;
    thread_control_block* _tcb;

    // State machine transition matrix
    //
    //   Initial       Next         Async?   Event         Notes
    //
    //   unstarted     waiting      sync     start()       followed by wake()
    //   unstarted     prestarted   sync     start()       before scheduler startup
    //
    //   prestarted    unstarted    sync     scheduler startup  followed by start()
    //
    //   waiting       waking       async    wake()
    //   waiting       running      sync     wait_until cancelled (predicate became true before context switch)
    //   waiting       sending_lock async    wake_lock()   used for ensuring the thread does not wake
    //                                                     up while we call receive_lock()
    //
    //   sending_lock  waking       async    mutex::unlock()
    //
    //   running       waiting      sync     prepare_wait()
    //   running       queued       sync     context switch
    //   running       terminating  sync     destroy()      thread function completion
    //
    //   queued        running      sync     context switch
    //
    //   waking        queued       async    scheduler poll of incoming thread wakeup queue
    //   waking        running      sync     thread pulls self out of incoming wakeup queue
    //
    //   terminating   terminated   async    post context switch
    //
    // wake() on any state except waiting is discarded.

    enum class status {
        invalid,
        prestarted,
        unstarted,
        waiting,
        sending_lock,
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
        bool lock_sent = false;   // send_lock() was called for us
        std::atomic<status> st = { status::unstarted };
    };
    std::unique_ptr<detached_state> _detached_state;
    attr _attr;
    int _migration_lock_counter;
    arch_thread _arch;
    arch_fpu _fpu;
    unsigned int _id;
    std::atomic<bool> _interrupted;
    std::function<void ()> _cleanup;
    std::vector<std::unique_ptr<char[]>> _tls;
    thread_runtime::duration _total_cpu_time {0};
    void destroy();
    friend class thread_ref_guard;
    friend void thread_main_c(thread* t);
    friend class wait_guard;
    friend struct cpu;
    friend class timer;
    friend class thread_runtime_compare;
    friend struct arch_cpu;
    friend class thread_runtime;
    friend void migrate_enable();
    friend void migrate_disable();
    friend void ::smp_main();
    friend void ::smp_launch();
    friend void init(std::function<void ()> cont);
public:
    std::atomic<thread *> _joiner;
    thread_runtime::duration thread_clock() { return _total_cpu_time; }
    bi::set_member_hook<> _runqueue_link;
    // see cpu class
    lockless_queue_link<thread> _wakeup_link;
    static unsigned long _s_idgen;
    static thread *find_by_id(unsigned int id);

    static int numthreads();
    /**
     * Registers an std::function that will be called when a thread is torn
     * down.  This is useful, for example, to run code that needs to cleanup
     * resources acquired by a given thread, about which the thread has no
     * knowledge about
     *
     * In general, this will not run in the same context as the dying thread,
     * but rather from special scheduler methods. Therefore, one needs to be
     * careful about stack usage in here. Do not register notifiers that use a
     * lot of stack
     */
    static void register_exit_notifier(std::function<void (thread *)> &&n);
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
    thread_handle& operator=(const thread_handle& x) {
	_t.assign(x._t.read());
	return *this;
    }
    void reset(thread& t) { _t.assign(t._detached_state.get()); }
    void wake();
    void clear() { _t.assign(nullptr); }
    operator bool() const { return _t; }
    bool operator==(const thread_handle& x) const {
        return _t.read_by_owner() == x._t.read_by_owner();
    }
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
    osv::clock::uptime::time_point _last {
            osv::clock::uptime::time_point::max() };
    bi::set<timer_base, bi::base_hook<bi::set_base_hook<>>> _list;
    class callback_dispatch : private clock_event_callback {
    public:
        callback_dispatch();
        virtual void fired();
    };
    static callback_dispatch _dispatch;
};

std::chrono::nanoseconds osv_run_stats();

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
    osv::clock::uptime::time_point running_since;
    char* percpu_base;
    static cpu* current();
    void init_on_cpu();
    static void schedule();
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

// wait_for() support for predicates
//

template <typename Pred>
class predicate_wait_object {
public:
    predicate_wait_object(Pred pred, mutex* mtx = nullptr) : _pred(pred) {}
    void arm() {}
    void disarm() {}
    bool poll() { return _pred(); }
private:
    Pred _pred;
};


// only instantiate wait_object<Pred> if Pred is indeed a predicate
template <typename Pred>
class wait_object
    : public std::enable_if<std::is_same<bool, decltype((*static_cast<Pred*>(nullptr))())>::value,
                           predicate_wait_object<Pred>>::type
{
    using predicate_wait_object<Pred>::predicate_wait_object;
    // all contents from predicate_wait_object<>
};

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

class interruptible
{
public:
    static void prepare(thread* target_thread) throw()
        { target_thread->interrupted(false); }
    static void check(thread* target_thread) throw(int)
        { if(target_thread->interrupted()) throw int(EINTR); }
};

class noninterruptible
{
public:
    static void prepare(thread* target_thread) throw()
        {}
    static void check(thread* target_thread) throw()
        {}
};

template <class IntrStrategy, class Mutex, class Pred>
inline
void thread::do_wait_until(Mutex& mtx, Pred pred)
{
    assert(arch::irq_enabled());
    assert(preemptable());

    thread* me = current();

    IntrStrategy::prepare(me);

    while (true) {
        {
            wait_guard waiter(me);
            if (pred()) {
                return;
            }

            IntrStrategy::check(me);

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
    do_wait_until<noninterruptible>(mtx, pred);
}

template <class Pred>
inline
void thread::wait_until(mutex_t& mtx, Pred pred)
{
    do_wait_until<noninterruptible>(mtx, pred);
}

template <class Pred>
inline
void thread::wait_until(mutex_t* mtx, Pred pred)
{
    do_wait_until<noninterruptible>(mtx, pred);
}

template <class Pred>
inline
void thread::wait_until_interruptible(Pred pred)
{
    dummy_lock mtx;
    do_wait_until<interruptible>(mtx, pred);
}

inline bool thread::interrupted()
{
    return _interrupted.load(std::memory_order_relaxed);
}

inline void thread::interrupted(bool f)
{
    _interrupted.store(f, std::memory_order_relaxed);

    if(f) {
        wake();
    }
}

// thread::wait_for() accepts an optional mutex, followed by a
// number waitable objects.
//
// a waitable object's protocol is as follows:
//
// Given a waitable object 'wa', the class wait_object<waitable> defines
// the waiting protocol using instance methods:
//
//   wait_object(wa, mtx) - initialization; if mutex is not required for waiting it can be optional
//   poll() - check whether wa has finished waiting
//   arm() - prepare for waiting; typically registering wa on some list
//   disarm() - called after waiting is complete
//
// all of these are called with the mutex held, if supplied


// wait_object<T> must be specialized for the particular waitable.
template <typename T>
class wait_object;

template <typename... wait_object>
void arm(wait_object&... objs);

template <>
inline
void arm()
{
}

template <typename wait_object_first, typename... wait_object_rest>
inline
void arm(wait_object_first& first, wait_object_rest&... rest)
{
    first.arm();
    arm(rest...);
}

template <typename... wait_object>
bool poll(wait_object&... objs);

template <>
inline
bool poll()
{
    return false;
}

template <typename wait_object_first, typename... wait_object_rest>
inline
bool poll(wait_object_first& first, wait_object_rest&... rest)
{
    return first.poll() || poll(rest...);
}

template <typename... wait_object>
void disarm(wait_object&... objs);

template <>
inline
void disarm()
{
}

template <typename wait_object_first, typename... wait_object_rest>
inline
void disarm(wait_object_first& first, wait_object_rest&... rest)
{
    disarm(rest...);
    first.disarm();
}

template <typename Mutex, typename... wait_object>
inline
void thread::do_wait_for(Mutex& mtx, wait_object&&... wait_objects)
{
    if (poll(wait_objects...)) {
        return;
    }
    arm(wait_objects...);
    // must duplicate do_wait_until since gcc has a bug capturing parameter packs
    thread* me = current();
    while (true) {
        {
            wait_guard waiter(me);
            if (poll(wait_objects...)) {
                break;
            }
            release(mtx);
            me->wait();
        }
        if (me->_detached_state->lock_sent) {
            me->_detached_state->lock_sent = false;
            mtx.receive_lock();
        } else {
            acquire(mtx);
        }
    }
    disarm(wait_objects...);
}

template <typename... waitable>
inline
void thread::wait_for(mutex& mtx, waitable&&... waitables)
{
    do_wait_for(mtx, wait_object<typename std::remove_reference<waitable>::type>(waitables, &mtx)...);
}

template <typename... waitable>
inline
void thread::wait_for(waitable&&... waitables)
{
    dummy_lock mtx;
    do_wait_for(mtx, wait_object<typename std::remove_reference<waitable>::type>(waitables)...);
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
void thread::do_wake_with(Action action, unsigned allowed_initial_states_mask)
{
    WITH_LOCK(osv::rcu_read_lock) {
        auto ds = _detached_state.get();
        action();
        wake_impl(ds, allowed_initial_states_mask);
    }
}

template <class Rep, class Period>
void thread::sleep(std::chrono::duration<Rep, Period> duration)
{
    timer t(*current());
    t.set(duration);
    wait_until([&] { return t.expired(); });
}

template <class Action>
inline
void thread::wake_with(Action action)
{
    return do_wake_with(action, (1 << unsigned(status::waiting)));
}

template <class Action>
inline
void thread::wake_with_from_mutex(Action action)
{
    return do_wake_with(action, (1 << unsigned(status::waiting))
                              | (1 << unsigned(status::sending_lock)));
}

extern cpu __thread* current_cpu;

extern __thread unsigned exception_depth;

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

inline void migrate_disable()
{
    thread::current()->_migration_lock_counter++;
}

inline void migrate_enable()
{
    thread::current()->_migration_lock_counter--;
}


}

#endif /* SCHED_HH_ */
