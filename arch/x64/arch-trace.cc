/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "osv/trace.hh"
#include "osv/align.hh"
#include "cpuid.hh"
#include <stdlib.h>

static void patch_fence(void * dst, size_t size) {
    if (processor::features().clflush) {
        asm volatile (
                "mfence \n"
                "clflush (%0) \n"
                "mfence \n"
                : /*no output*/
                :"r"(dst));
    }
    if (align_down(dst, sizeof(void*)) != align_down(dst + size - 1, sizeof(void*))) {
        patch_fence(dst + size - 1, 1);
    }
}

/**
 * This little patch routine is only for modifying the small "fast-path" nop5/jmp instruction
 * in trace entries.
 * Its more complex than seems needed, we could probably in most cases get away with just an
 * unaligned CAS to do this, _BUT_ there is an old issue with code patching. At least older
 * AMD cpu:s have issues when patching code across a cache line, which basically means that
 * unless we can guarantee that we fit the whole rewrite into a single aligned CAS8 we formally
 * need to do this in two steps;
 * First patching the instruction into a jmp - 2, writing "the rest", then fix it up.
 *
 * It then follows that at the very least the instruction to be patched need to be aligned on
 * 2, so we can at least CAS the jmp.
 */
static void patch_trace_site(void * dst, const void * src, size_t size)
{
    assert((uintptr_t(dst) & 1) == 0 && "We're assuming at least alignment on 2");

    if ((uintptr_t(dst) & 1) != 0) {
        throw std::invalid_argument("Address must be aligned on 2");
    }

    constexpr const size_t ws = sizeof(void *);

    union {
        char buf[ws];
        uintptr_t qword;
    } replace;

    void * const aligned = align_down(dst, ws);
    const auto off = uintptr_t(dst) - uintptr_t(aligned);

    memcpy(replace.buf, aligned, ws);

    const bool twostage = aligned != align_down(dst + size - 1, ws);

    if (twostage) {
        // Cannot fit into an aligned write.
        // Step 1: Create worlds smallest loop.
        replace.buf[off] = 0xeb; // jmp
        replace.buf[off + 1] = 0xfe; // -2
    } else {
        assert(size <= ws - off);
        memcpy(replace.buf + off, src, size);
    }
    // No need to CAS or check success or not. We assume we are synchronized from caller,
    // thus the only contention is thread visibility and consistency.
    // Strictly speaking we could just do a store here, since aligned writes are "atomic"
    // on x86. But using a locked swap has the benefit of acting as am acquire barrier.
    // Need to use gcc intrinsic, since its not a std::atomic type

    __sync_lock_test_and_set(static_cast<volatile uintptr_t *>(aligned), replace.qword);

    if (twostage) {
        // Ensure the change is visible
        // Again, this is only actually needed on somewhat ancient AMD:s
        // with very short cache lines.
        patch_fence(dst, 1);

        // Now, copy the rest and swap things back.
        memcpy(dst + 2, src + 2, size - 2);

        patch_fence(dst + 2, size - 2);

        assert(size >= ws - off);
        memcpy(replace.buf + off, src, ws - off);

        __sync_lock_test_and_set(static_cast<volatile uintptr_t *>(aligned), replace.qword);
    }
}

void tracepoint_base::activate(const tracepoint_id &, void * patch_site, void * slow_path)
{
    auto dst = static_cast<char*>(slow_path);
    auto src = static_cast<char*>(patch_site) + 5;

    char bytes[5];

    bytes[0] = 0xe9; // jmp
    union {
        char buf[4];
        u32 dword;
    } u;

    u.dword = dst - src;
    std::copy(std::begin(u.buf), std::end(u.buf), std::begin(bytes) + 1);

    patch_trace_site(patch_site, bytes, 5);
}

void tracepoint_base::deactivate(const tracepoint_id &, void * patch_site, void * slow_path)
{
    const char nop5[5] = { 0x0f, 0x1f, 0x44, 0x00, 0x00, };
    patch_trace_site(patch_site, nop5, 5);
}

