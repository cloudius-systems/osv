#ifndef _OSV_LATCH_HH
#define _OSV_LATCH_HH

#include <condition_variable>
#include <mutex>

class latch
{
private:
    int _count;
    std::mutex _mutex;
    std::condition_variable _condvar;
public:
    latch(int count)
        : _count(count)
    {
    }

    void count_down()
    {
        std::lock_guard<std::mutex> l(_mutex);
        if (--_count == 0) {
            _condvar.notify_all();
        }
    }

    void await()
    {
        std::unique_lock<std::mutex> l(_mutex);
        while (_count > 0) {
            _condvar.wait(l);
        }
    }
};

#endif
