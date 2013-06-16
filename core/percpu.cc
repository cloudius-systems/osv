#include <osv/percpu.hh>
#include <string.h>

std::vector<void*> percpu_base{64};  // FIXME: move to sched::cpu

extern char _percpu_start[], _percpu_end[];

void percpu_init(unsigned cpu)
{
    assert(!percpu_base[cpu]);
    auto size = _percpu_end - _percpu_start;
    percpu_base[cpu] = malloc(size);
    memcpy(percpu_base[cpu], _percpu_start, size);
}
