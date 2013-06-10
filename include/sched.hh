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

class timer : public bi::set_base_hook<>, public bi::list_base_hook<> {
public:
    explicit timer(thread& t);
    ~timer();
    void set(u64 time);
    bool expired() const;
    void cancel();
    friend bool operator<(const timer& t1, const timer& t2);
private:
    void expire();
private:
    thread& _t;
    bool _expired;
    u64 _time;
    friend class timer_list;
};

class thread {
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
    static void sleep_until(u64 abstime);
    static void yield();
    static void exit() __attribute__((__noreturn__));
    static thread* current() __attribute((no_instrument_function));
    stack_info get_stack_info();
    cpu* tcpu() __attribute__((no_instrument_function));
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
    void suspend_timers();
    void resume_timers();
    static void on_thread_stack(thread* t);
    template <class Mutex, class Pred>
    static void do_wait_until(Mutex& mtx, Pred pred);
    struct dummy_lock {};
    friend void acquire(dummy_lock&) {}
    friend void release(dummy_lock&) {}
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
        terminated,
    };
    std::atomic<status> _status;
    attr _attr;
    cpu* _cpu;
    bool _timers_need_reload;
    bi::list<timer> _active_timers;
    arch_thread _arch;
    arch_fpu _fpu;
    unsigned long _id;
    u64 _vruntime;
    u64 _borrow;
    std::function<void ()> _cleanup;
    friend void thread_main_c(thread* t);
    friend class wait_guard;
    friend class cpu;
    friend class timer;
    friend class thread_runtime_compare;
    friend class arch_cpu;
    friend void ::smp_main();
    friend void ::smp_launch();
    friend void init(elf::tls_data tls, std::function<void ()> cont);
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
    void suspend(bi::list<timer>& t);
    void resume(bi::list<timer>& t);
private:
    friend class timer;
    bi::set<timer, bi::base_hook<bi::set_base_hook<>>> _list;
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
        return t1._vruntime - t1._borrow < t2._vruntime - t2._borrow;
    }
};

typedef bi::rbtree<thread,
                   bi::member_hook<thread,
                                   bi::set_member_hook<>,
                                   &thread::_runqueue_link>,
                   bi::compare<thread_runtime_compare>,
                   bi::constant_time_size<true> // for load estimation
                  > runqueue_type;

struct cpu {
    explicit cpu();
    unsigned id;
    struct arch_cpu arch;
    thread* bringup_thread;
    runqueue_type runqueue;
    timer_list timers;
    thread idle_thread;
    // for each cpu, a list of threads that are migrating into this cpu:
    typedef lockless_queue<thread, &thread::_wakeup_link> incoming_wakeup_queue;
    cpu_set incoming_wakeups_mask;
    incoming_wakeup_queue* incoming_wakeups;
    static cpu* current();
    void init_on_cpu();
    void schedule(bool yield = false);
    void handle_incoming_wakeups();
    void idle();
    void do_idle();
    void load_balance();
    unsigned load();
    void reschedule_from_interrupt(bool preempt = false);
    void enqueue(thread& t, u64 now);
};

void preempt();
void preempt_disable() __attribute__((no_instrument_function));
void preempt_enable() __attribute__((no_instrument_function));

thread* current();

class wait_guard {
public:
    wait_guard(thread* t) : _t(t) { t->prepare_wait(); }
    ~wait_guard() { _t->stop_wait(); }
private:
    thread* _t;
};

// does not return - continues to @cont instead
void init(elf::tls_data tls_data, std::function<void ()> cont);

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

extern cpu __thread* current_cpu;

inline cpu* thread::tcpu()
{
    return _cpu;
}

inline cpu* cpu::current()
{
    return thread::current()->tcpu();
}

extern std::vector<cpu*> cpus;

}

#endif /* SCHED_HH_ */
