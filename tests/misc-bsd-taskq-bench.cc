/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Microbenchmark of a BSD taskqueue dispatch, the path every ZFS zio issue
// takes: P producer threads each enqueue tasks (an embedded struct task, as
// the OpenZFS taskq shim uses) on one taskqueue with W worker threads, and
// we record, as log2(us) histograms:
//   enqueue  the taskqueue_enqueue() call (tq_mutex + insert + wakeup_one)
//   latency  enqueue to the task starting on a worker
// Usage: misc-bsd-taskq-bench [producers [workers [tasks-per-producer]]]

#include <bsd/porting/netport.h>
#include <bsd/sys/sys/priority.h>
#include <bsd/sys/sys/taskqueue.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>
#include <unistd.h>

namespace {

constexpr int nbucket = 20;

struct hist {
    std::atomic<uint64_t> n{0}, ns{0};
    std::atomic<uint64_t> b[nbucket] {};
    void add(uint64_t d) {
        n++;
        ns += d;
        int i = 0;
        while (i < nbucket - 1 && (d >> 10) >= (1ull << i)) {
            i++;
        }
        b[i]++;
    }
    // The upper bound (us) of the bucket holding quantile q.
    unsigned long pct(double q) const {
        uint64_t t = n.load(), acc = 0;
        for (int i = 0; i < nbucket; i++) {
            acc += b[i].load();
            if (t && acc >= q * t) {
                return i == 0 ? 1 : 1ul << i;
            }
        }
        return 0;
    }
    void print(const char *name) const {
        printf("%-8s n=%lu mean=%.2f us p50<=%lu p90<=%lu p99<=%lu us\n", name, n.load(),
               n.load() ? ns.load() / 1e3 / n.load() : 0.0, pct(.5), pct(.9), pct(.99));
        // The whole histogram: the count in each log2 bucket, by its upper bound.
        printf("%-8s hist", name);
        for (int i = 0; i < nbucket; i++) {
            if (b[i].load()) {
                printf(" <=%lu:%lu", i == 0 ? 1ul : 1ul << i, b[i].load());
            }
        }
        printf("\n");
    }
};

uint64_t now_ns()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

struct job {
    struct task t;
    uint64_t enq_ns;
    std::atomic<bool> done{false};
};

hist h_enq, h_lat;

void run_job(void *ctx, int)
{
    auto *j = static_cast<job *>(ctx);
    h_lat.add(now_ns() - j->enq_ns);
    j->done.store(true, std::memory_order_release);
}

} // namespace

int main(int argc, char **argv)
{
    int producers = argc > 1 ? atoi(argv[1]) : 16;
    int workers = argc > 2 ? atoi(argv[2]) : 8;
    int per = argc > 3 ? atoi(argv[3]) : 20000;

    struct taskqueue *tq = taskqueue_create("bench", M_WAITOK, taskqueue_thread_enqueue, &tq);
    if (!tq || taskqueue_start_threads(&tq, workers, PWAIT, "%s", "bench") != 0) {
        printf("FAIL: cannot create the taskqueue\n");
        return 1;
    }

    auto t0 = std::chrono::steady_clock::now();
    std::vector<std::thread> ps;
    for (int p = 0; p < producers; p++) {
        ps.emplace_back([&] {
            // Each producer owns 32 jobs and reuses one once it has run, as
            // a zio's embedded entry is reused only after its task ran.
            std::vector<job> jobs(32);
            for (int k = 0; k < per; k++) {
                job &j = jobs[k % jobs.size()];
                if (k >= (int)jobs.size()) {
                    // Sleep, not spin: a producer spinning on a vCPU a worker
                    // needs would stall the very task it waits for.
                    while (!j.done.load(std::memory_order_acquire)) {
                        usleep(20);
                    }
                }
                j.done.store(false, std::memory_order_relaxed);
                TASK_INIT(&j.t, 0, run_job, &j);
                uint64_t a = now_ns();
                j.enq_ns = a;
                taskqueue_enqueue(tq, &j.t);
                h_enq.add(now_ns() - a);
            }
            for (auto &j : jobs) {
                while (!j.done.load(std::memory_order_acquire)) {
                    usleep(20);
                }
            }
        });
    }
    for (auto &p : ps) {
        p.join();
    }
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    taskqueue_free(tq);

    printf("BENCH producers=%d workers=%d tasks=%d secs=%.2f rate=%.0f/s\n", producers, workers,
           producers * per, secs, producers * per / secs);
    h_enq.print("enqueue");
    h_lat.print("latency");
    bool ok = h_lat.n.load() == (uint64_t)producers * per;
    printf("%s: every task ran once\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
