#ifndef __PERCPU_WORKER_HH__
#define __PERCPU_WORKER_HH__

#include <list>
#include <functional>
#include <osv/percpu.hh>
#include <osv/condvar.h>
#include <sched.hh>

#define PCPU_WORKERITEM(name, lambda) \
    worker_item name __attribute__((section(".percpu_workers"))) { lambda }

class workman;
class worker_item {
public:
    explicit worker_item(std::function<void ()> handler);
    void signal(sched::cpu* cpu);
    void clear_work(sched::cpu* cpu);
    void set_finished(sched::cpu* cpu);
    bool have_work(sched::cpu* cpu);
private:
    std::atomic<bool> _have_work[sched::max_cpus];
public:
    std::function<void ()> _handler;
    friend class workman;
};

// invokes work items in a per cpu manner
class workman {
public:
    bool signal(sched::cpu* cpu);
private:
    static sched::cpu::notifier _cpu_notifier;
    static void pcpu_init();

    // per CPU thread that invokes worker items
    static percpu<std::atomic<bool>> _duty;
    static percpu<std::atomic<bool>> _ready;
    static percpu<sched::thread *> _work_sheriff;
    static void call_of_duty(void);
};

#endif // !__PERCPU_WORKER_HH__
