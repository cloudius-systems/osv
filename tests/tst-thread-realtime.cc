#include <atomic>
#include <string>
#include <iostream>

#include <osv/elf.hh>
#include <osv/sched.hh>

static std::string name = "tst-thr-wrk";
static int tests = 0, fails = 0;

static void report(bool ok, std::string msg)
{
    ++tests;
    fails += !ok;
    std::cout << (ok ? "PASS" : "FAIL") << ": " << msg << "\n";
}

static int fac(int n)
{
    if (n == 0) {
        return 0;
    } else if (n == 1) {
        return 1;
    } else {
        return fac(n - 1) + fac(n - 2);
    }
}

static bool preempts_equalized()
{
    constexpr int slice = 250;
    constexpr int num_threads = 5;
    constexpr int preempts = 20;
    std::atomic<bool> stop_threads(false);
    sched::thread *threads[num_threads];

    sched::cpu *c = sched::cpu::current();
    for (int i = 0; i < num_threads; i++) {
        threads[i] = sched::thread::make([&]{
                while (!stop_threads.load()) {
                    fac(10);
                }
        }, sched::thread::attr().name(name));

        threads[i]->pin(c);
        threads[i]->set_realtime_priority(1);
        threads[i]->set_realtime_time_slice(std::chrono::milliseconds(slice));
        threads[i]->start();
    }

    auto runtime = std::chrono::milliseconds((slice * num_threads) * preempts);
    sched::thread::sleep(runtime);
    stop_threads = true;

    std::vector<long> num_preempts(num_threads);
    for (int i = 0; i < num_threads; i++) {
        num_preempts[i] = threads[i]->stat_preemptions.get();
    }

    bool ok = true;

    // Fuzzy comparison with `num_preempts` which allows for some
    // divergence from the expected amount of preempts to account
    // for scheduler overhead.
    constexpr int slack = preempts * 0.10; // 10% slack
    for (auto n : num_preempts) {
        if (n != preempts && !(n == preempts-slack || n == preempts+slack)) {
            ok = false;
            break;
        }
    }

    for (int i = 0; i < num_threads; i++) {
        delete threads[i];
    }

    return ok;
}

static bool priority_precedence()
{
    constexpr int slice = 100000000;

    std::atomic<bool> high_prio_stop(false);
    std::atomic<bool> high_prio_yielded(false);
    std::atomic<bool> low_prio_enqueued(false);

    sched::thread *main_thread = sched::thread::current();
    sched::thread *high_prio = sched::thread::make([&]{
        while (!high_prio_stop.load()) {
            fac(10);

            if (low_prio_enqueued && !high_prio_yielded) {
                sched::thread::yield();
                main_thread->wake_with([&] { high_prio_yielded.store(true); });
            }
        }
    });

    static std::atomic<bool> low_prio_stop;
    sched::thread *low_prio = sched::thread::make([&]{
        while (!low_prio_stop.load()) {
            fac(10);
        }
    });

    sched::cpu *c = sched::cpu::current();
    high_prio->pin(c);
    low_prio->pin(c);

    // If these are commented, the test will realiably fail.
    high_prio->set_realtime_priority(2);
    low_prio->set_realtime_priority(1);

    // The higher priority thread has a time slice, but since there is no other thread
    // with the same priority, it should monopolize the CPU and lower_prio shouldn't run.
    high_prio->set_realtime_time_slice(sched::thread_realtime::duration(slice));

    high_prio->start();
    low_prio->start();

    // Ensure that the high priority thread yields once while the low priority thread
    // is also in the runqueue. Without real-time scheduling the low priority thread
    // should then get the CPU.
    while (low_prio->get_status() != sched::thread::status::queued) {
        // busy wait until low_prio is enqueued.
    }
    low_prio_enqueued.store(true);
    sched::thread::wait_until([&] { return high_prio_yielded.load(); });

    auto runtime = std::chrono::nanoseconds(slice * 3);
    sched::thread::sleep(runtime);

    // Since both threads are pinned to the CPU and the higher priority
    // thread is always runnable, the lower priority thread should starve.
    bool ok = high_prio->thread_clock().count() > 0 &&
        low_prio->thread_clock().count() == 0;

    low_prio_stop = true;
    high_prio_stop = true;

    delete high_prio;
    delete low_prio;

    return ok;
}

int main(int ac, char** av)
{
    // Ensure that the main thread doesn't starve.
    sched::thread *cur = sched::thread::current();
    cur->set_realtime_priority(10);

    report(priority_precedence(), "priority_precedence");
    report(preempts_equalized(), "preempts_equalized");
    std::cout << "SUMMARY: " << tests << " tests, " << fails << " failures\n";
}

// Because we're using wake_with() and wait_until(), this executable must not
// cause sleepable lazy symbol resolutions or on-demand paging:
OSV_ELF_MLOCK_OBJECT();
