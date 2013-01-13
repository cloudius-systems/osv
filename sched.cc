#include "sched.hh"
#include <list>
#include "mutex.hh"
#include <mutex>
#include "debug.hh"

namespace sched {

std::list<thread*> runqueue;

thread __thread * s_current;

elf::tls_data tls;

}

#include "arch-switch.hh"

namespace sched {

void schedule()
{
    thread* p = thread::current();
    if (!p->_waiting) {
        return;
    }
    assert(!runqueue.empty());
    thread* n = runqueue.front();
    runqueue.pop_front();
    assert(!n->_waiting);
    n->_on_runqueue = false;
    n->switch_to();
}

thread::thread(std::function<void ()> func, bool main)
    : _func(func)
    , _on_runqueue(!main)
    , _waiting(false)
{
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
    debug("thread dtor");
}

void thread::prepare_wait()
{
    _waiting = true;
}

void thread::wake()
{
    if (!_waiting) {
        return;
    }
    _waiting = false;
    if (!_on_runqueue) {
        _on_runqueue = true;
        runqueue.push_back(this);
        schedule();
    }
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

void thread::stop_wait()
{
    _waiting = false;
}

thread::stack_info thread::get_stack_info()
{
    return stack_info { _stack, sizeof(_stack) };
}

void init(elf::program& prog)
{
    tls = prog.tls();
}

}
