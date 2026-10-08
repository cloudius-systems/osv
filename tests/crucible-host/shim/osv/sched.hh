/* Copyright (C) 2026 Greg Burd. */
#pragma once
#include <thread>
#include <functional>
class unused;
namespace sched {
class thread {
    std::function<void()> fn;
    std::thread worker;
    bool detach_;
public:
    struct attr { bool d=false; attr& detached() { d=true; return *this; } };
    thread(std::function<void()> f, bool d): fn(f), detach_(d) {}
    static thread* make(std::function<void()> f) { return new thread(f, false); }
    static thread* make(std::function<void()> f, attr a) { return new thread(f, a.d); }
    void start() { worker=std::thread(fn); if(detach_) worker.detach(); }
    void join() { worker.join(); }
    template<class R,class P> static void sleep(std::chrono::duration<R,P> d) { std::this_thread::sleep_for(d); }
};
}
