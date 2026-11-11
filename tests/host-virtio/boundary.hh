// Copyright (C) 2026 Greg Burd
// BSD license; see LICENSE in the top-level directory.
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
#include <unistd.h>
using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using mutex = std::mutex;
#define WITH_LOCK(m) if (std::lock_guard<mutex> guard{m}; true)
struct trace_noop { template<class... T> void operator()(const T&...) const {} };
#define TRACEPOINT(name, ...) trace_noop name
#define tprintf_d(...) ((void)0)
#define tprintf_i(...) ((void)0)
#define tprintf_w(...) ((void)0)
#define tprintf_e(...) ((void)0)
#define VIRTIO_ALIGN(x,a) (((x)+(a)-1)&~((a)-1))
inline bool is_power_of_two(unsigned n) { return n && !(n & (n-1)); }
namespace mmu {
using phys = uintptr_t;
inline u64 virt_to_phys(void* p) { return reinterpret_cast<uintptr_t>(p); }
inline void* phys_to_virt(u64 p) { return reinterpret_cast<void*>(p); }
template<class F> void virt_to_phys(void* p, size_t n, F f) { f(virt_to_phys(p), n); }
}
namespace memory {
inline void* alloc_phys_contiguous_aligned(size_t n, size_t a) {
    void* p = nullptr;
    if (posix_memalign(&p, a, n)) std::abort();
    return p;
}
inline void free_phys_contiguous_aligned(void* p) { free(p); }
}
namespace sched {
inline std::atomic<bool> producer_waiting{false};
inline thread_local bool consumer = false;
inline thread_local unsigned waits = 0;
struct finished {};
struct thread {
    static thread* current() { static thread t; return &t; }
    template<class F> static void wait_until(F f) {
        if (consumer && waits++) throw finished{};
        while (!f()) std::this_thread::yield();
    }
};
struct thread_handle {
    void reset(thread&) { producer_waiting.store(true); }
    void clear() {}
    void wake_from_kernel_or_with_irq_disabled() {}
};
}
