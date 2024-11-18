/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/preempt-lock.hh>
#include <osv/migration-lock.hh>
#include <future>
#include <chrono>
#include <osv/elf.hh>
OSV_ELF_MLOCK_OBJECT();

using _clock = std::chrono::high_resolution_clock;

struct dummy_lock
{
    void lock() {}
    void unlock() {}
};

template<typename Lock>
double time(Lock& lock)
{
    const std::chrono::seconds test_duration(3);

    long count = 0;
    auto start = _clock::now();
    auto end_after = start + test_duration;

    while (_clock::now() < end_after) {
        for (int i = 0; i < 1000; i++) {
            WITH_LOCK(lock) {
                count++;
            }
        }
    }

    auto duration = _clock::now() - start;
    return double(std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count()) / count;
}

template<typename Lock>
void test(const char *name, Lock& lock)
{
    printf("%-10s = %7.3f ns/cycle\n", name, time(lock));
}

int main(int argc, char const *argv[])
{
    test("dummy", *new dummy_lock);
    test("preempt", preempt_lock);
    test("migrate", migration_lock);
    test("mutex", *new mutex);
    return 0;
}
