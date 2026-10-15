// Copyright (C) 2026 Greg Burd
// BSD license; see LICENSE in the top-level directory.
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <mutex>
#include <vector>
#include <unistd.h>
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using mutex = std::mutex;
#define CHECK(x) do { if (!(x)) { std::cerr << "FAIL: " #x << '\n'; std::exit(1); } } while (0)
struct trace_noop { template<class... T> void operator()(const T&...) const {} };
#define TRACEPOINT(name, ...) trace_noop name
#define VIRTIO_ALIGN(x,a) (((x)+(a)-1)&~((a)-1))
inline bool is_power_of_two(unsigned n) { return n && !(n & (n-1)); }
static std::function<void(const char*)> at_fatal;
// The real OSv abort overload never returns. Only this boundary exits 86;
// sanitizer errors, SIGABRT, watchdog expiry and normal return are failures.
[[noreturn]] void abort(const char* fmt, ...) {
    char reason[256];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(reason, sizeof(reason), fmt, args);
    va_end(args);
    CHECK(n >= 0 && size_t(n) < sizeof(reason));
    CHECK(bool(at_fatal));
    at_fatal(reason);
    std::_Exit(86);
}
namespace mmu {
using phys = uintptr_t;
inline u64 virt_to_phys(void* p) { return reinterpret_cast<uintptr_t>(p); }
inline void* phys_to_virt(u64 p) { return reinterpret_cast<void*>(p); }
template<class F> void virt_to_phys(void* p, size_t n, F f) { f(virt_to_phys(p), n); }
}
namespace memory {
static unsigned allocations = 0, frees = 0;
inline void* alloc_phys_contiguous_aligned(size_t n, size_t a) {
    void* p = nullptr;
    CHECK(posix_memalign(&p, a, n) == 0);
    ++allocations;
    return p;
}
inline void free_phys_contiguous_aligned(void* p) { ++frees; free(p); }
}
namespace sched {
struct thread {
    static thread* current() { static thread t; return &t; }
    template<class F> static void wait_until(F f) { CHECK(f()); }
};
struct thread_handle {
    void reset(thread&) {}
    void clear() {}
    void wake_from_kernel_or_with_irq_disabled() {}
};
}
namespace virtio {
class virtio_driver {
public:
    size_t get_vring_alignment() { return 4096; }
    bool get_indirect_buf_cap() { return true; }
    bool get_event_idx_cap() { return false; }
    bool kick(unsigned) { return true; }
};
}
