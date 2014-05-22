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
    latch(int count)
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
};

#endif
