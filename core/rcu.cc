/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/rcu.hh>
#include <osv/mutex.h>
#include <osv/semaphore.hh>
#include <vector>
#include <boost/algorithm/cxx11/all_of.hpp>

namespace osv {

rcu_lock_type rcu_read_lock;
preempt_lock_in_rcu_type preempt_lock_in_rcu;
rcu_lock_in_preempt_type rcu_read_lock_in_preempt_disabled;

namespace rcu {

mutex mtx;
std::vector<std::function<void ()>> callbacks;
void collect_garbage();
sched::thread* garbage_collector_thread;

class cpu_quiescent_state_thread {
public:
    cpu_quiescent_state_thread(sched::cpu* cpu);
    void request(uint64_t generation);
    bool check(uint64_t generation);
private:
    void work();
private:
    sched::thread _t;
    std::atomic<uint64_t> _generation = { 0 };
    std::atomic<uint64_t> _request = { 0 };
};

std::vector<cpu_quiescent_state_thread*> cpu_quiescent_state_threads;

// FIXME: hot-remove cpus
// FIXME: locking for the vector
sched::cpu::notifier cpu_notifier([] {
        cpu_quiescent_state_threads.push_back(new cpu_quiescent_state_thread(sched::cpu::current()));
});

cpu_quiescent_state_thread::cpu_quiescent_state_thread(sched::cpu* cpu)
    : _t([=] { work(); }, sched::thread::attr(cpu))
{
    _t.start();
}

void cpu_quiescent_state_thread::request(uint64_t generation)
{
    auto r = _request.load(std::memory_order_relaxed);
    while (generation > r && !_request.compare_exchange_weak(r, generation, std::memory_order_relaxed)) {
        // nothing to do
    }
    _t.wake();
}

bool cpu_quiescent_state_thread::check(uint64_t generation)
{
    return _generation.load(std::memory_order_relaxed) >= generation;
}

void cpu_quiescent_state_thread::work()
{
    while (true) {
        sched::thread::wait_until([&] {
            return _generation.load(std::memory_order_relaxed) <  _request.load(std::memory_order_relaxed);
        });
        auto r = _request.load(std::memory_order_relaxed);
        _generation.store(r, std::memory_order_relaxed);
        garbage_collector_thread->wake();
    }
}

bool all_at_generation(decltype(cpu_quiescent_state_threads)& cqsts,
                       uint64_t generation)
{
    for (auto cqst : cqsts) {
        if (!cqst->check(generation)) {
            return false;
        }
    }
    return true;
}

void await_grace_period()
{
    static uint64_t generation = 0;
    ++generation;
    // copy cpu_quiescent_state_threads to prevent a hotplugged cpu
    // from changing the number of cpus we request a new generation on,
    // and the number of cpus we wait on
    // FIXME: better locking
    auto cqsts = cpu_quiescent_state_threads;
    for (auto cqst : cqsts) {
        cqst->request(generation);
    }
    sched::thread::wait_until([&cqsts] { return all_at_generation(cqsts, generation); });
}

void collect_garbage()
{
    while (true) {
        std::vector<std::function<void ()>> now;
        WITH_LOCK(mtx) {
            sched::thread::wait_until(mtx, [] { return !callbacks.empty(); });
            now = std::move(callbacks);
        }
        await_grace_period();
        for (auto& c : now) {
            c();
        }
    }
}

}

using namespace rcu;

void rcu_defer(std::function<void ()>&& func)
{
    WITH_LOCK(mtx) {
        callbacks.push_back(func);
    }
    garbage_collector_thread->wake();
}

void rcu_synchronize()
{
    semaphore s{0};
    rcu_defer([](semaphore* s) { s->post(); }, &s);
    s.wait();
}

void rcu_init()
{
    garbage_collector_thread = new sched::thread(collect_garbage);
    garbage_collector_thread->start();
}

}
