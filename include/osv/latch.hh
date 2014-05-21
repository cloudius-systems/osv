/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
#ifndef _OSV_LATCH_HH
#define _OSV_LATCH_HH

#include <condition_variable>
#include <mutex>
#include <atomic>

class latch
{
private:
    std::atomic<int> _count;
    std::mutex _mutex;
    std::condition_variable _condvar;
public:
    latch(int count = 1)
        : _count(count)
    {
    }

    void count_down()
    {
        std::unique_lock<std::mutex> l(_mutex);
        if (_count.fetch_add(-1, std::memory_order_release) == 1) {
            _condvar.notify_all();
        }
    }

    bool is_released()
    {
        return _count.load(std::memory_order_acquire) <= 0;
    }

    void await()
    {
        if (is_released()) {
            return;
        }

        std::unique_lock<std::mutex> l(_mutex);
        while (!is_released()) {
            _condvar.wait(l);
        }
    }

    template<typename Rep, typename Period>
    bool await_for(const std::chrono::duration<Rep, Period>& duration)
    {
        if (is_released()) {
            return true;
        }

        std::unique_lock<std::mutex> l(_mutex);
        return _condvar.wait_for(l, duration, [&] () -> bool { return is_released(); });
    }
};

class thread_barrier
{
private:
    latch _latch;
public:
    thread_barrier(int count)
        : _latch(count)
    {
    }

    void arrive()
    {
        _latch.count_down();
        _latch.await();
    }
};

#endif
