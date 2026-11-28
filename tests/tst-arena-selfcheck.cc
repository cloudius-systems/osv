// Host-side self-check of the fork_arena chunk math (no OSv needed).
// Compile: g++ -std=gnu++14 -o /tmp/aselftest tests/tst-arena-selfcheck.cc && /tmp/aselftest
// Verifies, with the arena's constants (16-byte header, 64 size classes up to
// 2 MiB, 64 MiB extents over PML4 slots 96..127): header/base recovery,
// alignment, no metadata collision with the free-stack link, usable_size, the
// free-stack head word round trip and its tag, and extent indexing.
#include <cstdint>
#include <cstddef>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>

static inline uintptr_t align_up(uintptr_t v, uintptr_t a) { return (v + a - 1) & ~(a - 1); }

constexpr size_t header_size = 16;
constexpr size_t max_alloc = 2u << 20;
constexpr unsigned num_classes = 64, linear_classes = 8, linear_max_shift = 7;
constexpr size_t granule = 16, linear_max = linear_classes * granule;
constexpr uintptr_t arena_base = 96ull << 39, arena_end = 128ull << 39;
constexpr unsigned extent_shift = 26, ptr_bits = 43;
constexpr uint64_t ptr_mask = (uint64_t(1) << ptr_bits) - 1;

static size_t class_size_for(unsigned idx)
{
    if (idx < linear_classes) {
        return granule * (idx + 1);
    }
    unsigned octave = (idx - linear_classes) / 4, step = (idx - linear_classes) % 4;
    size_t base = linear_max << octave;
    return base + (base >> 2) * (step + 1);
}

static unsigned class_for(size_t total)
{
    if (total <= linear_max) {
        return unsigned((total + granule - 1) / granule) - 1;
    }
    unsigned lg = 63u - unsigned(__builtin_clzll(total - 1));
    unsigned step = unsigned(((total - 1) >> (lg - 2)) & 3u);
    return linear_classes + (lg - linear_max_shift) * 4u + step;
}

static uintptr_t head_ptr(uint64_t h) { return uintptr_t(h & ptr_mask) << 4; }
static uint64_t head_make(uintptr_t p, uint64_t old)
{
    return ((uint64_t(p) >> 4) & ptr_mask) | (((old >> ptr_bits) + 1) << ptr_bits);
}

// Simulate carve at a given chunk base, then recover.
static void check(uintptr_t base, size_t size, size_t alignment)
{
    if (alignment < 16) alignment = 16;
    size_t need = header_size + size + (alignment > header_size ? alignment : 0);
    unsigned idx = class_for(need);
    assert(idx < num_classes);
    size_t class_size = class_size_for(idx);
    assert(class_size >= need);
    uintptr_t user = align_up(base + header_size, alignment);
    // chunk must contain [base, base+class_size); user data must fit.
    assert(user + size <= base + class_size);
    // alignment honored
    assert((user % alignment) == 0);
    // metadata slots inside [base, user)
    uintptr_t hdr = user - 8;
    uintptr_t baseslot = user - 16;
    assert(baseslot >= base && hdr + 8 <= user);
    // A freed chunk's free-stack link lives at base (0..8). It may overlap the
    // saved base only when they are the same word, which free() has already
    // read; it never overlaps the header, so a double free still finds it.
    assert(base + 8 <= hdr);
    // usable_size
    size_t usable = class_size - (user - base);
    assert(usable >= size);
    // a chunk never crosses an extent when carved from an extent-aligned cursor
    assert(class_size <= max_alloc && max_alloc < (size_t(1) << extent_shift));
}

int main()
{
    for (unsigned i = 1; i < num_classes; i++) {
        assert(class_size_for(i) > class_size_for(i - 1));
        assert(class_size_for(i) % granule == 0);
        assert(class_for(class_size_for(i)) == i);
        assert(class_for(class_size_for(i - 1) + 1) == i);
    }
    assert(class_size_for(num_classes - 1) == max_alloc);

    // A spread of sizes and alignments, at a few carve bases.
    for (uintptr_t base = arena_base; base < arena_base + (1 << 20); base += 4096 + 48) {
        for (size_t sz : {1u, 8u, 15u, 16u, 17u, 100u, 1000u, 4096u, 100000u, 1u << 20}) {
            for (size_t al : {0u, 8u, 16u, 32u, 64u, 256u, 4096u, 65536u}) {
                size_t a = al > 16 ? al : 16;
                size_t need = header_size + sz + (a > header_size ? a : 0);
                if (need > max_alloc) continue;
                check(base & ~uintptr_t(15), sz, al);
            }
        }
    }

    // Head word: every 16-aligned arena address round-trips, the tag changes
    // on every update, and an empty head reads as null.
    assert(arena_end <= (uint64_t(1) << (ptr_bits + 4)));
    assert(head_ptr(0) == 0);
    uint64_t h = 0;
    for (uintptr_t p : {arena_base, arena_base + 16, arena_end - 16, uintptr_t(0x3fffdeadbee0ull)}) {
        uint64_t n = head_make(p, h);
        assert(head_ptr(n) == p);
        assert((n >> ptr_bits) == (h >> ptr_bits) + 1);
        h = n;
    }
    uint64_t wrap = head_make(arena_base, ~uint64_t(0));
    assert(head_ptr(wrap) == arena_base);   // the tag wraps, the pointer survives

    // Extents tile the arena range exactly.
    assert(((arena_end - arena_base) >> extent_shift) == 262144);
    assert(((arena_end - 1 - arena_base) >> extent_shift) == 262143);

    printf("fork_arena selfcheck: PASS\n");
    return 0;
}
