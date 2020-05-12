/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "sse.hh"
#include <x86intrin.h>
#include <osv/initialize.hh>
#include <stdint.h>

// We want to use the PALIGNR SSSE3 instruction to copy relatively
// misaligned data (e.g. 1k from address 0x1000006 to address 0x2000007,
// a relative misalignment of 1 byte.  The PALIGNR instruction helps
// by combining two 16-byte SSE registers into a 32-byte temporary,
// shifting it right, and returning a 16-byte result.
//
// Unfortunately, the shift count operand to PALIGNR is immediate, not
// a register, so we must generate 15 different versions (one for each
// possible shift), plus a non-shifting versions for the case when the
// data is aligned.
//
// The pipeline requires two temporary registers (the last source operand
// is reused as the first input to the next pass, and we need a temporary
// due to the destructive nature of SSE instructions), so each loop we
// copy 14*16 bytes.
//
// We also generate versions with 1*16-13*16 bytes copied for the tail end
// of the loop.
//
// Finally, we need non-SSE copies for 0-15 bytes to align the destination
// at the start and end.

// We need to use horrible template tricks to do all this.

template <unsigned Width, unsigned Shift>
struct unaligned_copier {

    // registers 2:Width+1: data
    // register  1: last register from previous loop
    // register  0: temporary copy of register 1
    using reg_file = sse_register_file<Width+2>;
    reg_file regs;


    // load registers 2:Width+1
    template <unsigned N = 0, typename Ignore = void>
    struct loader;

    // shift registers Width+1:1 into Width+1:2
    template <unsigned N = 0, typename Ignore = void>
    struct shifter;

    // store registers Width+1:2
    template <unsigned N = 0, typename Ignore = void>
    struct storer;

    // set up registers for the first pass
    void init_pipeline(const __m128i* src);
    // prepare for next pass
    void adjust_pipeline();
    // save register 1 since it will be clobbered
    void duplicate_last();

    void operator()(__m128i* dest, const __m128i* src, size_t iterations);
};

template <unsigned Width, unsigned Shift>
template <unsigned N, typename Ignore>
struct unaligned_copier<Width, Shift>::loader {
    void operator()(reg_file& regs, const __m128i* src) {
        constexpr auto Pos = Width - N - 1;  // load end-to-start since that's use order
        constexpr auto Reg = 2 + Pos;
        xmm<Reg>(regs) = _mm_load_si128(src + Pos);
        loader<N+1>()(regs, src);
    }
};

template <unsigned Width, unsigned Shift>
template <typename Ignore>
struct unaligned_copier<Width, Shift>::loader<Width, Ignore> {
    using reg_file = typename unaligned_copier<Width, Shift>::reg_file;
    void operator()(reg_file& regs, const __m128i* src) {}
};

template <unsigned Width, unsigned Shift>
template <unsigned N, typename Ignore>
struct unaligned_copier<Width, Shift>::shifter {
    void operator()(reg_file& regs) {
        constexpr auto Pos = Width - N - 1;
        constexpr auto Reg = 2 + Pos;
        // since PALIGNR destroys the most-significant register in the pair,
        // we need to work end-to-start.
        xmm<Reg>(regs) = _mm_alignr_epi8(xmm<Reg>(regs), xmm<Reg-1>(regs), Shift);
        shifter<N+1>()(regs);
    }
};

template <unsigned Width, unsigned Shift>
template <typename Ignore>
struct unaligned_copier<Width, Shift>::shifter<Width, Ignore> {
    using reg_file = typename unaligned_copier<Width, Shift>::reg_file;
    void operator()(reg_file& regs) {}
};

template <unsigned Width, unsigned Shift>
template <unsigned N, typename Ignore>
struct unaligned_copier<Width, Shift>::storer {
    void operator()(reg_file& regs, __m128i* dest) {
        constexpr auto Pos = Width - N - 1;  // store end-to-start since that's use order
        constexpr auto Reg = 2 + Pos;
        _mm_store_si128(dest + Pos, xmm<Reg>(regs));
        storer<N+1>()(regs, dest);
    }
};

template <unsigned Width, unsigned Shift>
template <typename Ignore>
struct unaligned_copier<Width, Shift>::storer<Width, Ignore> {
    using reg_file = typename unaligned_copier<Width, Shift>::reg_file;
    void operator()(reg_file& regs, __m128i* dest) {}
};

template <unsigned Width, unsigned Shift>
inline
void unaligned_copier<Width, Shift>::init_pipeline(const __m128i* src)
{
    xmm<1>(regs) = _mm_load_si128(src);
}

template <unsigned Width, unsigned Shift>
inline
void unaligned_copier<Width, Shift>::adjust_pipeline()
{
    xmm<1>(regs) = xmm<0>(regs);
}

template <unsigned Width, unsigned Shift>
inline
void unaligned_copier<Width, Shift>::duplicate_last()
{
    xmm<0>(regs) = xmm<Width+1>(regs);
}

// main copy loop
template <unsigned Width, unsigned Shift>
inline
void unaligned_copier<Width, Shift>::operator()(__m128i* dest, const __m128i* src, size_t iterations) {
    init_pipeline(src);
    ++src;
    for (size_t i = 0; i < iterations; ++i) {
        loader<>()(regs, src);
        duplicate_last();
        shifter<>()(regs);
        storer<>()(regs, dest);
        adjust_pipeline();
        dest += Width;
        src += Width;
    }
}

// degenerate case: no shifting needed
template <unsigned Width>
struct unaligned_copier<Width, 0> {

    using reg_file = sse_register_file<Width>;
    reg_file regs;

    void operator()(__m128i* dest, const __m128i* src, size_t iterations) {
        for (size_t i = 0; i != iterations; ++i) {
            regs.load_aligned(src);
            regs.store_aligned(dest);
            src += Width;
            dest += Width;
        }
    }

};

// convert unaligned_copier::operator() into a function template,
// that we can store in a look-up table
template <unsigned Width, unsigned Shift>
void unaligned_copy(void* dest, const void* src, size_t iterations)
{
    unaligned_copier<Width, Shift> copy;
    copy((__m128i*)dest, (const __m128i*)src, iterations);
}

using unaligned_copy_fn = void (*)(void* dest, const void* src, size_t iterations);

using unaligned_copier_with_fixed_width = std::array<unaligned_copy_fn, 16>;

// initialized_array<> wants a template to generate values to populate
// the array with:
template <unsigned Width, unsigned Shift>
struct init_unaligned_copier {
    static constexpr unaligned_copy_fn value = unaligned_copy<Width+1, Shift>;
};

template <size_t Width>
struct init_unaligned_copiers {
    template <size_t Shift>
    using init = init_unaligned_copier<Width, Shift>;
    static constexpr auto value = initialized_array<unaligned_copy_fn, 16,
            make_index_list<16>, init>();
};

// 14*16 lookup table (by copy width, and by byte shift)
initialized_array<unaligned_copier_with_fixed_width, 14,
    make_index_list<14>, init_unaligned_copiers> unaligned_copiers;

// sub 16 byte copy for loop head and tail
template <unsigned Width>
void do_short_copy(void* dest, const void* src)
{
    struct data {
        char x[Width];
    } __attribute__((packed));

    *(data*)dest = *(data*)src;
};

// degenerate case
template <>
void do_short_copy<0>(void* dest, const void* src)
{
}

using short_copy_fn = void (*)(void* dest, const void* src);

template <size_t Width>
struct init_short_copier {
    static constexpr short_copy_fn value = do_short_copy<Width>;
};

initialized_array<short_copy_fn, 16,
    make_index_list<16>, init_short_copier> short_copiers;

void ssse3_unaligned_copy(void* dest, const void* src, size_t n)
{
    auto o_dest = reinterpret_cast<uintptr_t>(dest) & 15;
    auto o_src = reinterpret_cast<uintptr_t>(src) & 15;

    // make sure destination is aligned
    if (o_dest) {
        auto fixup = 16 - o_dest;
        short_copiers[fixup](dest, src);
        dest += fixup;
        src += fixup;
        n -= fixup;
        o_src = reinterpret_cast<uintptr_t>(src) & 15;
    }

    // use large width copier to copy as much as possible
    auto iterations = n / (14 * 16);
    unaligned_copiers[13][o_src](dest, src - o_src, iterations);
    dest += iterations * (14 * 16);
    src += iterations * (14 * 16);
    n -= iterations * (14 * 16);

    // use narrower copier while we can
    auto remain = n / 16;
    if (remain) {
        unaligned_copiers[remain-1][o_src](dest, src - o_src, 1);
        dest += remain * 16;
        src += remain * 16;
        n -= remain * 16;
    }

    // copy any tail
    short_copiers[n](dest, src);
}
