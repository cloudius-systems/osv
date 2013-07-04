#include <osv/percpu.hh>
#include <string.h>

extern char _percpu_start[], _percpu_end[], _percpu_sec_end[];
std::vector<void*> percpu_base{64};  // FIXME: move to sched::cpu

void percpu_init(unsigned cpu)
{
    auto max_size = _percpu_sec_end - _percpu_start;
    auto pcpu_size = _percpu_end - _percpu_start;
    assert(pcpu_size * (cpu+1) < max_size);
    percpu_base[cpu] = _percpu_start + cpu*pcpu_size;

    if (cpu != 0) {
        memcpy(percpu_base[cpu], _percpu_start, pcpu_size);
    }
}
