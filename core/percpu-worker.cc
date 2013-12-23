/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <debug.hh>
#include <sched.hh>
#include <osv/trace.hh>
#include <osv/percpu.hh>
#include <osv/percpu-worker.hh>

TRACEPOINT(trace_pcpu_worker_sheriff_started, "");;
TRACEPOINT(trace_pcpu_worker_item_invoke, "item=%p", worker_item*);
TRACEPOINT(trace_pcpu_worker_item_signal, "item=%p, dest_cpu=%d", worker_item*, unsigned);
TRACEPOINT(trace_pcpu_worker_item_wait, "item=%p", worker_item*);
TRACEPOINT(trace_pcpu_worker_item_end_wait, "item=%p", worker_item*);
TRACEPOINT(trace_pcpu_worker_item_set_finished, "item=%p, dest_cpu=%d", worker_item*, unsigned);

sched::cpu::notifier workman::_cpu_notifier(workman::pcpu_init);

PERCPU(std::atomic<bool>, workman::_duty);
PERCPU(std::atomic<bool>, workman::_ready);
PERCPU(sched::thread*, workman::_work_sheriff);

extern char _percpu_workers_start[];
extern char _percpu_workers_end[];

workman workman_instance;

worker_item::worker_item(std::function<void ()> handler)
{
    _handler = handler;
    for (unsigned i=0; i < sched::max_cpus; i++) {
        _have_work[i].store(false, std::memory_order_relaxed);
    }
}

void worker_item::signal(sched::cpu* cpu)
{
    trace_pcpu_worker_item_signal(this, cpu->id);
    _have_work[cpu->id].store(true, std::memory_order_relaxed);
    workman_instance.signal(cpu);
}

bool worker_item::have_work(sched::cpu* cpu)
{
    return (_have_work[cpu->id].load(std::memory_order_acquire));
}

void worker_item::set_finished(sched::cpu* cpu)
{
    trace_pcpu_worker_item_set_finished(this, cpu->id);
}

void worker_item::clear_work(sched::cpu* cpu)
{
    _have_work[cpu->id].store(false, std::memory_order_release);
}

bool workman::signal(sched::cpu* cpu)
{
    if (!(*_ready).load(std::memory_order_relaxed)) {
        return false;
    }

    //
    // let the sheriff know that he have to do what he have to do.
    // we simply set _duty=true and wake the sheriff
    //
    // when we signal a worker_item, we set 2 variables to true, the per
    // worker_item's per-cpu _have_work variable and the global _duty variable
    // of the cpu's sheriff we are signaling.
    //
    // we want the sheriff to see _duty=true only after _have_work=true.
    // in case duty=true will be seen before _have_work=true, we may miss
    // it in the sheriff thread.
    //
    (*(_duty.for_cpu(cpu))).store(true, std::memory_order_release);
    (*_work_sheriff.for_cpu(cpu))->wake();

    return true;
}

void workman::call_of_duty(void)
{
    (*_ready).store(true, std::memory_order_relaxed);
    trace_pcpu_worker_sheriff_started();

    while (true) {
        sched::thread::wait_until([&] {
            return ((*_duty).load(std::memory_order_relaxed) == true);
        });

        (*_duty).store(false, std::memory_order_relaxed);

        // Invoke all work items that needs handling. FIXME: O(n)
        sched::cpu* current = sched::cpu::current();
        worker_item* wi = reinterpret_cast<worker_item*>(_percpu_workers_start);
        worker_item* end = reinterpret_cast<worker_item*>(_percpu_workers_end);

        while (wi != end) {

            // Invoke worker item
            if (wi->have_work(current)) {
                trace_pcpu_worker_item_invoke(wi);
                wi->clear_work(current);
                wi->_handler();
                wi->set_finished(current);
            }

            wi++;
        }
    }
}

void workman::pcpu_init()
{
    (*_duty).store(false, std::memory_order_relaxed);

    // Create PCPU Sheriff
    *_work_sheriff = new sched::thread([] { workman::call_of_duty(); },
        sched::thread::attr().pin(sched::cpu::current()));

    (*_work_sheriff)->start();
}
