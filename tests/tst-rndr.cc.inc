// Copyright (C) 2026 Greg Burd
// BSD license; see LICENSE in the top-level directory.
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>

constexpr int PAGE_SIZE = 4096, HARVESTSIZE = 16, RANDOM_PURE_RDRAND = 14;
#define MIN(a, b) std::min(a, b)
struct random_hardware_source { const char* ident; int source; int (*read)(void*, int); };
static int rndr_read(void*, int);
struct harvest { uint64_t somecounter; uint8_t entropy[HARVESTSIZE]; int size, bits, source; };
using event_proc_f = void (*)(harvest*);
struct live_entropy_sources { random_hardware_source* rsource; };
static live_entropy_sources sources;
#define LIST_FOREACH(p, head, entries) for (p = (head); p; p = nullptr)
static int les_lock;
static void sx_slock(int*) {}
static void sx_sunlock(int*) {}
static uint64_t get_cyclecount() { return 1; }
namespace processor {
struct features_type { bool rndr = true; };
static features_type features() { return {}; }
}
struct device {};
struct uio { long uio_resid; uint8_t* out; };
static int uiomove(const void* p, int n, uio* u) {
    memcpy(u->out, p, n); u->out += n; u->uio_resid -= n; return 0;
}
static int blocked(int) { return EWOULDBLOCK; }
struct adaptor { bool seeded; int (*block)(int); int (*read)(void*, int); };
static adaptor context{false, blocked, nullptr};
static adaptor* random_adaptor = &context;
static unsigned calls, failures, successes;
static uint64_t value = 0x123456789abcdef0ULL;
static uint64_t* next_draw() {
    static uint64_t draw[2];
    ++calls;
    bool ok;
    if (successes) { --successes; ok = true; }
    else if (failures) { --failures; ok = false; }
    else { ok = true; }
    draw[0] = ok ? value : 0;
    draw[1] = ok ? 0 : 0x40000000;
    return draw;
}
// KERNEL_CODE
static int credited, events;
static void credit(harvest* e) {
    credited += e->bits; ++events;
    assert(e->size == 8 || e->size == 16);
    uint64_t word; memcpy(&word, e->entropy, 8); assert(word == value);
}
static void reset(unsigned fail, unsigned good = 0) {
    failures = fail; successes = good; calls = 0; credited = 0; events = 0;
}
int main() {
    uint64_t out[2];
    reset(0); out[0] = 0;
    assert(rndr_read(out, 8) == 8 && out[0] == value && calls == 1);
    // A genuine successful zero is valid; failure is identified by Z alone.
    value = 0; reset(0);
    assert(rndr_read(out, 8) == 8 && out[0] == 0);
    value = 0x123456789abcdef0ULL;
    reset(3); out[0] = ~0ULL;
    assert(rndr_read(out, 8) == 8 && out[0] == value && calls == 4);
    reset(100); out[0] = ~0ULL;
    assert(rndr_read(out, 8) == 0 && out[0] == ~0ULL && calls == 11);
    reset(100, 1); out[0] = out[1] = ~0ULL;
    assert(rndr_read(out, 16) == 8 && out[0] == value && out[1] == ~0ULL);
    reset(100); uint8_t bytes[9]; memset(bytes, 0xa5, sizeof(bytes));
    uio u{9, bytes};
    assert(random_read(nullptr, &u, 0) == EIO && u.uio_resid == 9);
    for (auto c : bytes) assert(c == 0xa5);
    reset(100, 1); u = {9, bytes};
    assert(random_read(nullptr, &u, 0) == EIO && u.uio_resid == 1 && bytes[8] == 0xa5);
    sources.rsource = &arm_rndr;
    reset(100); live_entropy_sources_feed(1, credit);
    assert(credited == 0 && events == 0);
    reset(100, 1); live_entropy_sources_feed(1, credit);
    assert(credited == 32 && events == 1);
    reset(0); live_entropy_sources_feed(1, credit);
    assert(credited == 64 && events == 1);
    puts("PASS: RNDR success/zero/retry/exhaustion/partial/device/entropy credit");
}
