#include "arch-cpu.hh"
#include "sched.hh"
#include "debug.hh"

namespace sched {

inline void arch_cpu::enter_exception()
{
    if (in_exception) {
        abort("nested exception");
    }
    in_exception = true;
    auto& s = initstack.stack;
    set_exception_stack(s, sizeof(s));
}

inline void arch_cpu::exit_exception()
{
    auto& s = exception_stack;
    set_exception_stack(s, sizeof(s));
    in_exception = false;
}

exception_guard::exception_guard()
{
    sched::cpu::current()->arch.enter_exception();
}

exception_guard::~exception_guard()
{
    sched::cpu::current()->arch.exit_exception();
}

}
