/* Copyright (C) 2026 Greg Burd. */
#pragma once
#include <condition_variable>
#include "mutex.h"
class condvar {
    std::condition_variable cv;
public:
    void wake_all() { cv.notify_all(); }
    void wake_one() { cv.notify_one(); }
    void wait(mutex* m) {
        std::unique_lock<mutex> lock(*m, std::adopt_lock);
        cv.wait(lock); lock.release();
    }
    template<class R, class P> void wait(mutex* m, std::chrono::duration<R,P> d) {
        std::unique_lock<mutex> lock(*m, std::adopt_lock);
        cv.wait_for(lock, d); lock.release();
    }
};
