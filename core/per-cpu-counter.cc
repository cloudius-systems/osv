#include <osv/per-cpu-counter.hh>
#include <osv/mutex.h>
#include <debug.hh>

PERCPU(ulong*, per_cpu_counter::_counters);

namespace {

const size_t max_counters = 1000;   // FIXME: allow auto-expand later

static std::vector<bool> used_indices(max_counters);
mutex mtx;

unsigned allocate_index()
{
    std::lock_guard<mutex> guard{mtx};
    auto i = std::find(used_indices.begin(), used_indices.end(), false);
    if (i == used_indices.end()) {
        abort("out of per-cpu counters");
    }
    *i = true;
    return i - used_indices.begin();
}

void free_index(unsigned index)
{
    std::lock_guard<mutex> guard{mtx};
    assert(used_indices[index]);
    used_indices[index] = false;
}

}

per_cpu_counter::per_cpu_counter()
    : _index(allocate_index())
{
    for (auto cpu : sched::cpus) {
        (*_counters.for_cpu(cpu))[_index] = 0;
    }
}

per_cpu_counter::~per_cpu_counter()
{
    free_index(_index);
}

void per_cpu_counter::increment()
{
    sched::preempt_disable();
    ++(*_counters)[_index];
    sched::preempt_enable();
}

ulong per_cpu_counter::read()
{
    ulong sum = 0;
    for (auto cpu : sched::cpus) {
        sum += (*_counters.for_cpu(cpu))[_index];
    }
    return sum;
}

void per_cpu_counter::init_on_cpu()
{
    *_counters = new ulong[max_counters];
}

sched::cpu::notifier per_cpu_counter::_cpu_notifier(per_cpu_counter::init_on_cpu);
