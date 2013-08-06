#ifndef SCHED_HH_
#define SCHED_HH_

#include "arch-thread-state.hh"
#include "arch-cpu.hh"
#include <functional>
#include "tls.hh"
#include "elf.hh"
#include "drivers/clockevent.hh"
#include <boost/intrusive/set.hpp>
#include <boost/intrusive/list.hpp>
#include <osv/mutex.h>
#include <atomic>
#include "osv/lockless-queue.hh"
#include <list>

extern "C" {
void smp_main();
};
void smp_launch();

namespace sched {

class thread;
class cpu;
class timer;
class timer_list;
class cpu_mask;
class thread_runtime_compare;

void schedule(bool yield = false);

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

class thread : private timer_base::client {
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
    template <class Pred>
    inline void wake_with(Pred pred);
    static void sleep_until(s64 abstime);
    static void yield();
    static void exit() __attribute__((__noreturn__));
    static thread* current() __attribute((no_instrument_function));
    stack_info get_stack_info();
    cpu* tcpu() const __attribute__((no_instrument_function));
    void join();
    void set_cleanup(std::function<void ()> cleanup);
    unsigned long id() __attribute__((no_instrument_function)); // guaranteed unique over system lifetime
private:
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
    static void on_thread_stack(thread* t);
    template <class Mutex, class Pred>
    static void do_wait_until(Mutex& mtx, Pred pred);
    struct dummy_lock {};
    friend void acquire(dummy_lock&) {}
    friend void release(dummy_lock&) {}
    template <typename T> T& remote_thread_local_var(T& var);
    void* do_remote_thread_local_var(void* var);
private:
    virtual void timer_fired() override;
private:
    std::function<void ()> _func;
    thread_state _state;
    thread_control_block* _tcb;
    enum class status {
        invalid,
        unstarted,
        waiting,
        running,
        queued,
        waking, // between waiting and queued
        terminating, // temporary state used in complete()
        terminated,
    };
    std::atomic<status> _status;
    attr _attr;
    cpu* _cpu;
    arch_thread _arch;
    arch_fpu _fpu;
    unsigned long _id;
    s64 _vruntime;
    static const s64 max_vruntime = std::numeric_limits<s64>::max();
    std::function<void ()> _cleanup;
    // When _ref_counter reaches 0, the thread can be deleted.
    // Starts with 1, decremented by complete() and also temporarily modified
    // by ref() and unref().
    std::atomic_uint _ref_counter;
    void ref();
    void unref();
    friend class thread_ref_guard;
    friend void thread_main_c(thread* t);
    friend class wait_guard;
    friend class cpu;
    friend class timer;
    friend class thread_runtime_compare;
    friend class arch_cpu;
    friend void ::smp_main();
    friend void ::smp_launch();
    friend void init(std::function<void ()> cont);
public:
    thread* _joiner;
    bi::set_member_hook<> _runqueue_link;
    // see cpu class
    lockless_queue_link<thread> _wakeup_link;
    // for the debugger
    bi::list_member_hook<> _thread_list_link;
    static unsigned long _s_idgen;
private:
    class reaper;
    friend class reaper;
    static reaper* _s_reaper;
    friend void init_detached_threads_reaper();
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
        return t1._vruntime < t2._vruntime;
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
    void* percpu_base;
    static cpu* current();
    void init_on_cpu();
    void schedule(bool yield = false);
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
    void enqueue(thread& t, bool waking = false);
    void init_idle_thread();
    void update_preemption_timer(thread* current, s64 now, s64 run);
    virtual void timer_fired() override;
    class notifier;
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
    friend class cpu;
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

// About ref(), unref() and and wake_with():
//
// Consider one thread doing:
//     wait_until([&] { return *x == 0; })
// the almost-correct way to wake it is:
//     *x = 0;
//     t->wake();
// This is only "almost" correct, because doing *x = 0 may already cause the
// thread to wake up (if it wasn't sleeping yet, or because of a spurious
// wakeup) and may decide to exit, in which case t->wake() may crash. So to be
// safe we need to use ref(), which prevents a thread's destruction until a
// matching unref():
//     t->ref();
//     *x = 0;
//     t->wake();
//     t->unref();
// wake_with is a convenient one-line shortcut for the above four lines,
// with a syntax mirroring that of wait_until():
//     t->wake_with([&] { *x = 0; });

class thread_ref_guard {
public:
    thread_ref_guard(thread* t) : _t(t) { t->ref(); }
    ~thread_ref_guard() { _t->unref(); }
private:
    thread* _t;
};

template <class Action>
inline
void thread::wake_with(Action action)
{
    // TODO: Try first to disable preemption and if thread and current are on
    // the same CPU, we don't need the disable_exit_guard (with its slow atomic
    // operations) because we know the thread can't run and exit.
    thread_ref_guard guard(this);
    action();
    wake();
}

extern cpu __thread* current_cpu;

inline cpu* thread::tcpu() const
{
    return _cpu;
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
