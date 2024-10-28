#include <atomic>
#include <string>
#include <iostream>
#include <osv/sched.hh>

#define TIME_SLICE 100000

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

bool runtime_equalized()
{
    constexpr int num_threads = 5;
    constexpr int switches = 3;
    static std::atomic<bool> stop_threads;
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
        threads[i]->set_realtime_time_slice(sched::thread_realtime::duration(TIME_SLICE));
        threads[i]->start();
    }

    auto runtime = sched::thread_realtime::duration(TIME_SLICE * num_threads * switches);
    sched::thread::sleep(runtime);
    stop_threads = true;

    bool ok = true;
    long prev_switches = -1;
    for (int i = 0; i < num_threads; i++) {
        long switches = threads[i]->stat_switches.get();
        if (prev_switches != -1 && prev_switches != switches) {
            ok = false;
            break;
        }
        prev_switches = switches;
    }

    for (int i = 0; i < num_threads; i++) {
        delete threads[i];
    }

    return ok;
}

bool priority_precedence()
{
    static std::atomic<bool> high_prio_stop;
    sched::thread *high_prio = sched::thread::make([&]{
        while (!high_prio_stop.load()) {
            fac(10);
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

    high_prio->set_realtime_priority(2);
    low_prio->set_realtime_priority(1);

    // The higher priority thread has a time slice, but since there is no other thread
    // with the same priority, it should monopolize the CPU and lower_prio shouldn't run.
    high_prio->set_realtime_time_slice(sched::thread_realtime::duration(TIME_SLICE));

    high_prio->start();
    low_prio->start();

    auto runtime = sched::thread_realtime::duration(TIME_SLICE * 3);
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

    report(runtime_equalized(), "runtime_equalized");
    report(priority_precedence(), "priority_precedence");
    std::cout << "SUMMARY: " << tests << " tests, " << fails << " failures\n";
}
