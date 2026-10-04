// Copyright (C) 2026 Greg Burd
// BSD license; see LICENSE in the top-level directory.
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <functional>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
using namespace std;
using u32 = uint32_t;
using u16 = uint16_t;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "CHECK failed: %s\n", #c); exit(1); } } while (0)
struct failure { string reason; };
[[noreturn]] void abort(const char* fmt, ...) {
    char message[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(message, sizeof(message), fmt, ap);
    va_end(ap);
    throw failure{message};
}
struct mutex { bool held = true; };
struct dropped {
    mutex& m;
    explicit dropped(mutex& m): m(m) { CHECK(m.held); m.held = false; }
    ~dropped() { m.held = true; }
    explicit operator bool() const { return true; }
};
#define DROP_LOCK(m) if (dropped guard{m})
namespace sched {
struct thread { template<class F> static void wait_until(F f) { CHECK(f()); } };
}
struct vring_used_elem { u32 _id, _len; };
#define trace_vring_get_buf_elem(...) ((void)0)
struct vring {
    // One writable descriptor, ID 0, no NEXT/INDIRECT chain. Only length is hostile.
    struct { void* address; size_t length; unsigned flags; } descriptor{};
    struct { atomic<u16> _idx{0}; vring_used_elem _used_elements[1]{}; } used;
    decltype(used)* _used = &used;
    unsigned _num = 1, _used_ring_host_head = 0;
    void* _cookie[1]{};
    vector<int> _sg_vec;
    void* submitted = nullptr;
    size_t capacity = 0;
    u32 returned = 0;
    unsigned finalized = 0;
    function<void()> after_submit;
    bool ready = false;
    void init_sg() { _sg_vec.clear(); }
    void add_in_sg(void* p, size_t n) { submitted = p; capacity = n; _sg_vec.push_back(0); }
    bool add_buf(void* p) {
        CHECK(p == submitted && _sg_vec.size() == 1);
        descriptor = {p, capacity, 2}; // VRING_DESC_F_WRITE only
        _cookie[0] = p;
        return true;
    }
    bool avail_ring_has_room(size_t) { return true; }
    bool used_ring_can_gc() { return false; }
    void get_buf_gc() { CHECK(false); }
    void kick() {
        CHECK(descriptor.address == submitted && descriptor.length == capacity);
        CHECK(descriptor.flags == 2 && _cookie[0] == submitted);
        // Simulate writes strictly within the valid submitted descriptor.
        for (size_t i = 0; i < capacity; ++i) static_cast<char*>(submitted)[i] = char(i + 1);
        if (after_submit) after_submit();
        used._used_elements[0] = {0, returned};
        used._idx.store(1);
        ready = true;
    }
    bool used_ring_not_empty() const { return ready; }
    void* get_buf_elem(u32* len);
    void get_buf_finalize() { CHECK(_cookie[0] == nullptr); ++finalized; }
};
// ACTUAL_GET_BUF_ELEM
namespace virtio {
struct rng {
    static constexpr size_t _pool_size = 64;
    vector<char> _entropy;
    vring* _queue;
    mutex _mtx;
    void wait_for_queue(vring* q, bool (vring::*pred)() const) {
        CHECK(!_mtx.held && (q->*pred)());
    }
    void refill();
};
// ACTUAL_REFILL
}
int main(int argc, char** argv) {
    CHECK(argc == 4);
    size_t capacity = stoul(argv[1]);
    u32 length = stoul(argv[2]);
    bool drain = stoi(argv[3]);
    CHECK(capacity > 0 && capacity <= 64);
    vring queue;
    queue.returned = length;
    virtio::rng rng;
    rng._queue = &queue;
    rng._entropy.assign(64 - capacity, char(0x33));
    if (drain) queue.after_submit = [&] { CHECK(!rng._mtx.held); rng._entropy.clear(); };
    bool failed = false;
    try {
        rng.refill();
    } catch (const failure& f) {
        failed = true;
        char wanted[128];
        snprintf(wanted, sizeof(wanted), "virtio-rng: used length %u exceeds submitted capacity %zu", length, capacity);
        CHECK(f.reason == wanted);
        CHECK(length > capacity);
        CHECK(queue.finalized == 0);
        CHECK(rng._entropy.size() == (drain ? 0 : 64 - capacity));
        for (char c : rng._entropy) CHECK(c == char(0x33));
    }
    CHECK(queue.capacity == capacity && queue._cookie[0] == nullptr && rng._mtx.held);
    CHECK(failed == (length > capacity));
    if (!failed) {
        CHECK(queue.finalized == 1);
        size_t prefix = drain ? 0 : 64 - capacity;
        CHECK(rng._entropy.size() == prefix + length && rng._entropy.size() <= 64);
        for (size_t i = 0; i < prefix; ++i) CHECK(rng._entropy[i] == char(0x33));
        for (size_t i = prefix; i < rng._entropy.size(); ++i) CHECK(rng._entropy[i] == char(i - prefix + 1));
    }
    printf("PASS capacity=%zu length=%u drain=%d rejected=%d\n", capacity, length, drain, failed);
}
