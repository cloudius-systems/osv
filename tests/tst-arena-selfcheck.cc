// Host-side self-check of the fork_arena chunk math (no OSv needed).
// Compile: g++ -std=gnu++14 -DARENA_SELFTEST -o /tmp/aselftest tests/tst-arena-selfcheck.cc && /tmp/aselftest
// Verifies: header/base recovery, alignment, no metadata collision, usable_size.
#include <cstdint>
#include <cstddef>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>

static inline uintptr_t align_up(uintptr_t v, uintptr_t a) { return (v + a - 1) & ~(a - 1); }

constexpr size_t min_class_shift = 5, max_class_shift = 21;
constexpr size_t header_size = 32;
constexpr uint32_t chunk_magic = 0x464b4152;
struct chunk_header { uint32_t class_shift; uint32_t magic; };

static unsigned class_for(size_t total) {
    unsigned s = min_class_shift;
    while ((size_t(1) << s) < total) s++;
    return s;
}

// Simulate carve at a given chunk base, then recover.
static void check(uintptr_t base, size_t size, size_t alignment) {
    if (alignment < 16) alignment = 16;
    size_t need = header_size + size + (alignment > header_size ? alignment : 0);
    unsigned s = class_for(need);
    size_t class_size = size_t(1) << s;
    uintptr_t user = align_up(base + header_size, alignment);
    // chunk must contain [base, base+class_size); user data must fit.
    assert(user + size <= base + class_size);
    // alignment honored
    assert((user % alignment) == 0);
    // metadata slots inside [base, user)
    uintptr_t hdr = user - sizeof(chunk_header);
    uintptr_t baseslot = user - 16;
    assert(hdr >= base && baseslot >= base && baseslot + 8 <= user && hdr + 8 <= user);
    // free_node.next lives at base (offset 0); must not overlap baseslot/hdr while allocated,
    // and while allocated we don't write base. No collision needed at offset 0 vs 16/24.
    assert(base + 8 <= baseslot);  // next-ptr region (0..8) clear of stored base (16..24)
    // usable_size
    size_t usable = class_size - (user - base);
    assert(usable >= size);
}

int main() {
    // A spread of sizes and alignments, at a few bump bases.
    for (uintptr_t base = 0x300000000000ull; base < 0x300000000000ull + (1<<20); base += 4096) {
        for (size_t sz : {1u, 8u, 15u, 16u, 17u, 100u, 1000u, 4096u, 100000u, 1u<<20}) {
            for (size_t al : {0u, 8u, 16u, 32u, 64u, 256u, 4096u}) {
                size_t need = header_size + sz + ((al>16?al:16) > header_size ? (al>16?al:16) : 0);
                if (need > (2u<<20)) continue;
                check(base, sz, al);
            }
        }
    }
    printf("fork_arena selfcheck: PASS\n");
    return 0;
}
