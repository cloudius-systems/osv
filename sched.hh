#ifndef SCHED_HH_
#define SCHED_HH_

#include "arch-thread-state.hh"
#include <functional>
#include "tls.hh"
#include "elf.hh"
#include "drivers/clockevent.hh"
#include <boost/intrusive/set.hpp>

namespace sched {

class thread;
void schedule();

extern "C" {
    void thread_main_c(thread* t);
};

class thread {
public:
    explicit thread(std::function<void ()> func, bool main = false);
    ~thread();
    template <class Pred>
    static void wait_until(Pred pred);
    void wake();
    static thread* current();
    struct stack_info;
    stack_info get_stack_info();
private:
    void main();
    void switch_to();
    void prepare_wait();
    void wait();
    void stop_wait();
    void init_stack();
    void setup_tcb();
    void setup_tcb_main();
    static void on_thread_stack(thread* t);
    void switch_to_thread_stack();
private:
    std::function<void ()> _func;
    thread_state _state;
    thread_control_block* _tcb;
    bool _on_runqueue;
    bool _waiting;
    char _stack[64*1024] __attribute__((aligned(16)));
    friend void thread_main_c(thread* t);
    friend class wait_guard;
    friend void schedule();
};

struct thread::stack_info {
    void* begin;
    size_t size;
};

thread* current();

namespace bi = boost::intrusive;

class timer;

class timer_list : private clock_event_callback {
public:
    timer_list();
    virtual void fired();
private:
    friend class timer;
    bi::set<timer, bi::base_hook<bi::set_base_hook<>>> _list;
};

class timer : public bi::set_base_hook<> {
public:
    explicit timer(thread& t);
    ~timer();
    void set(u64 time);
    bool expired() const;
    void cancel();
    friend bool operator<(const timer& t1, const timer& t2);
private:
    thread& _t;
    bool _expired;
    u64 _time;
    friend class timer_list;
};

class wait_guard {
public:
    wait_guard(thread* t) : _t(t) { t->prepare_wait(); }
    ~wait_guard() { _t->stop_wait(); }
private:
    thread* _t;
};

void init(elf::program& prog);

template <class Pred>
void thread::wait_until(Pred pred)
{
    thread* me = current();
    wait_guard waiter(me);
    while (!pred()) {
        me->wait();
    }
}

}

#endif /* SCHED_HH_ */
