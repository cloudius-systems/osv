#include <osv/rcu.hh>
#include <osv/mutex.h>
#include <vector>
#include <boost/algorithm/cxx11/all_of.hpp>

namespace osv {

rcu_lock_type rcu_read_lock;

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

bool all_at_generation(uint64_t generation)
{
    for (auto cqst : cpu_quiescent_state_threads) {
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
    for (auto cqst : cpu_quiescent_state_threads) {
        cqst->request(generation);
    }
    sched::thread::wait_until([] { return all_at_generation(generation); });
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

void rcu_init()
{
    garbage_collector_thread = new sched::thread(collect_garbage);
    garbage_collector_thread->start();
}

}
