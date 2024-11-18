/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */


#include <string.h>
#include <stdint.h>
#include "cpuid.hh"
#include <osv/string.h>
#include <osv/prio.hh>
#include "memcpy_decode.hh"
#include <assert.h>
#include <osv/initialize.hh>
#include "sse.hh"
#include <x86intrin.h>

extern "C"
void *memcpy_base(void *__restrict dest, const void *__restrict src, size_t n);
extern "C"
void *memset_base(void *__restrict dest, int c, size_t n);

extern "C" void memcpy_fixup_byte(exception_frame *ef, size_t fixup)
{
    assert(fixup <= ef->rcx);
    ef->rdi += fixup;
    ef->rsi += fixup;
    ef->rcx -= fixup;
}

extern "C" void memcpy_fixup_long(exception_frame *ef, size_t fixup)
{
    assert(fixup/sizeof(long) <= ef->rcx);
    ef->rdi += fixup;
    ef->rsi += fixup;
    ef->rcx -= fixup / sizeof(long);
}

// Please note that the arguments to those functions are passed by reference. That
// allow us to reuse the fact that the updates to src and dest will be held in the
// registers themselves, and avoid storing that temporarily.
//
// The generated code is significantly different:
//
// 0000000000330d40 <memcpy_repmov_old>:
// 330d40:       55                      push   %rbp
// 330d41:       48 89 d1                mov    %rdx,%rcx
// 330d44:       48 89 f8                mov    %rdi,%rax
// 330d47:       48 c1 e9 03             shr    $0x3,%rcx
// 330d4b:       48 89 e5                mov    %rsp,%rbp
// 330d4e:       f3 48 a5                rep movsq %ds:(%rsi),%es:(%rdi)
// 330d51:       83 e2 07                and    $0x7,%edx
// 330d54:       48 89 d1                mov    %rdx,%rcx
// 330d57:       f3 a4                   rep movsb %ds:(%rsi),%es:(%rdi)
// 330d59:       5d                      pop    %rbp
//
// Versus:
//
// 0000000000330d40 <memcpy_repmov_old>:
// 330d40:       55                      push   %rbp
// 330d41:       48 89 d1                mov    %rdx,%rcx
// 330d44:       48 89 f8                mov    %rdi,%rax
// 330d47:       49 89 f1                mov    %rsi,%r9
// 330d4a:       48 c1 e9 03             shr    $0x3,%rcx
// 330d4e:       48 89 e5                mov    %rsp,%rbp
// 330d51:       f3 48 a5                rep movsq %ds:(%rsi),%es:(%rdi)
// 330d54:       48 c1 e1 03             shl    $0x3,%rcx
// 330d58:       83 e2 07                and    $0x7,%edx
// 330d5b:       48 8d 3c 08             lea    (%rax,%rcx,1),%rdi
// 330d5f:       49 8d 34 09             lea    (%r9,%rcx,1),%rsi
// 330d63:       48 89 d1                mov    %rdx,%rcx
// 330d66:       f3 a4                   rep movsb %ds:(%rsi),%es:(%rdi)
// 330d68:       5d                      pop    %rbp
//
//
// Note how in the second version the arguments need to be updated to reflect
// the displacement after movsq, while in the first version, it just happens
// naturally.
static inline __always_inline void
repmovsq(void *__restrict &dest, const void *__restrict &src, size_t &n)
{
    asm volatile
       ("1: \n\t"
        "rep movsq\n\t"
        ".pushsection .memcpy_decode, \"ax\" \n\t"
        ".quad 1b, 8, memcpy_fixup_long\n\t"
        ".popsection\n"
            : "+D"(dest), "+S"(src), "+c"(n) : : "memory");
}

static inline __always_inline void
repmovsb(void *__restrict &dest, const void *__restrict &src, size_t &n)
{
    asm volatile
       ("1: \n\t"
        "rep movsb\n\t"
        ".pushsection .memcpy_decode, \"ax\" \n\t"
        ".quad 1b, 1, memcpy_fixup_byte\n\t"
        ".popsection\n"
            : "+D"(dest), "+S"(src), "+c"(n) : : "memory");
}

// Note that although we called this function "memcpy", we can actually
// call it also for overlapping dest and src, but only when dest<src.
// The implementation below must work in this case as well, and not only
// in the non-overlapping cases guaranteed by memcpy() implementations.
template <size_t N>
__attribute__((optimize("omit-frame-pointer")))
__attribute__((optimize("unroll-loops")))
void* do_small_memcpy(void *dest, const void *src)
{
#if __GNUC__ < 12
    // Until Gcc 12, the following assignment-based implementation worked also
    // for overlapping dest and src (as long as dest < src). It no longer
    // does, as noted in https://gcc.gnu.org/bugzilla/show_bug.cgi?id=108296
    // and in #1212. So this implementation cannot be used for Gcc 12.
    struct [[gnu::packed]] data {
        char x[N];
    };
    *static_cast<data*>(dest) = *static_cast<const data*>(src);
#else
    // This appears a naive and inefficient implementation of a byte-by-byte
    // copy, but turns out that for a fixed N the compiler generates
    // optimized code.
    for (unsigned int i = 0; i < N; i++) {
        ((char*)dest)[i] = ((const char*)src)[i];
    }
#endif
    return dest;
}

static constexpr int small_memcpy_lim = 64;

using memcpy_fn = void *(*)(void* dest, const void* src);

template <size_t Width>
struct init_small_copier {
    static constexpr memcpy_fn value = do_small_memcpy<Width>;
};

initialized_array<memcpy_fn, small_memcpy_lim,
    make_index_list<small_memcpy_lim>, init_small_copier> small_memcpy_table;

static inline void* small_memcpy(void *dest, const void *src, size_t n)
{
    return small_memcpy_table[n](dest, src);
}

template <unsigned N, typename loader, typename storer>
inline
void do_sse_memcpy_body(void* dest, const void* src)
{
    loader load;
    storer store;
    sse_register_file<16> sse;
    for (unsigned i = 0; i < N; ++i) {
        load(sse, reinterpret_cast<const __m128i*>(src) + i * 16);
        store(sse, reinterpret_cast<__m128i*>(dest) + i * 16);
    }
}

template <unsigned N, typename loader, typename storer>
inline
void do_sse_memcpy_tail(void* dest, const void* src)
{
    loader load;
    storer store;
    auto sse_dest = static_cast<__m128i*>(dest);
    auto sse_src = static_cast<const __m128i*>(src);
    sse_register_file<N> regs;
    load(regs, sse_src);
    store(regs, sse_dest);
}

template <unsigned N, typename loader, typename storer>
__attribute__((optimize("unroll-loops"), optimize("omit-frame-pointer")))
void do_sse_memcpy(void* dest, const void* src)
{
    do_sse_memcpy_body<N/16, loader, storer>(dest, src);
    do_sse_memcpy_tail<N % 16, loader, storer>(dest + N/16 * 256, src + N/16 * 256);
}

struct load_aligned {
    template <typename register_file, typename datum>
    void operator()(register_file& rf, const datum* src) { rf.load_aligned(src); }
};

struct load_unaligned {
    template <typename register_file, typename datum>
    void operator()(register_file& rf, const datum* src) { rf.load_unaligned(src); }
};

struct store_aligned {
    template <typename register_file, typename datum>
    void operator()(register_file& rf, datum* dest) { rf.store_aligned(dest); }
};

struct store_unaligned {
    template <typename register_file, typename datum>
    void operator()(register_file& rf, datum* dest) { rf.store_unaligned(dest); }
};

using static_copier_fn = void (*)(void*, const void*);

template <size_t N>
struct unaligned_copier {
    static auto constexpr value = do_sse_memcpy<N, load_unaligned, store_unaligned>;
};

template <size_t N>
struct aligned_copier {
    static auto constexpr value = do_sse_memcpy<N, load_aligned, store_aligned>;
};

// indices: [is 16-byte aligned][number of 16-byte segments]
static constexpr std::array<static_copier_fn, 64> sse_memcpy_table[2] = {
        initialized_array<static_copier_fn, 64, make_index_list<64>, unaligned_copier>(),
        initialized_array<static_copier_fn, 64, make_index_list<64>, aligned_copier>(),
};

static bool both_aligned(const void* dest, const void* src, size_t align)
{
    return ((reinterpret_cast<uintptr_t>(src)
        | reinterpret_cast<uintptr_t>(dest)) & (align - 1)) == 0;
}

static inline void* sse_memcpy(void* dest, const void* src, size_t n)
{
    sse_memcpy_table[both_aligned(dest, src, 16)][n/16](dest, src);
    small_memcpy(dest + (n & ~15), src + (n & ~15), n & 15);
    return dest;
}


extern "C"
[[gnu::optimize("omit-frame-pointer")]]
void *memcpy_repmov_old(void *__restrict dest, const void *__restrict src, size_t n)
{
    if (n < small_memcpy_lim) {
        return small_memcpy(dest, src, n);
    } else if (n < 1024) {
        return sse_memcpy(dest, src, n);
    } else {
        auto ret = dest;
        auto nw = n / 8;
        auto nb = n & 7;

        repmovsq(dest, src, nw);
        repmovsb(dest, src, nb);

        return ret;
    }
}

extern "C"
[[gnu::optimize("omit-frame-pointer")]]
void *memcpy_repmov(void *__restrict dest, const void *__restrict src, size_t n)
{
    if (n < small_memcpy_lim) {
        return small_memcpy(dest, src, n);
    } else if (n < 1024) {
        return sse_memcpy(dest, src, n);
    } else {
        auto ret = dest;
        repmovsb(dest, src, n);
        return ret;
    }
}

extern "C"
[[gnu::optimize("omit-frame-pointer")]]
void *memcpy_repmov_old_ssse3(void *__restrict dest, const void *__restrict src, size_t n)
{
    if (n < small_memcpy_lim) {
        return small_memcpy(dest, src, n);
    } else if (n < 1024) {
        return sse_memcpy(dest, src, n);
    } else if (n < 65536 && !both_aligned(dest, src, 16)) {
        ssse3_unaligned_copy(dest, src, n);
        return dest;
    } else {
        auto ret = dest;
        auto nw = n / 8;
        auto nb = n & 7;

        repmovsq(dest, src, nw);
        repmovsb(dest, src, nb);

        return ret;
    }
}

extern "C"
[[gnu::optimize("omit-frame-pointer")]]
void *memcpy_repmov_ssse3(void *__restrict dest, const void *__restrict src, size_t n)
{
    if (n < small_memcpy_lim) {
        return small_memcpy(dest, src, n);
    } else if (n < 1024) {
        return sse_memcpy(dest, src, n);
    } else if (n < 65536 && !both_aligned(dest, src, 16)) {
        ssse3_unaligned_copy(dest, src, n);
        return dest;
    } else {
        auto ret = dest;
        repmovsb(dest, src, n);
        return ret;
    }
}

extern "C"
void *(*resolve_memcpy())(void *__restrict dest, const void *__restrict src, size_t n)
{
    if (processor::features().repmovsb) {
        if (processor::features().ssse3) {
            return memcpy_repmov_ssse3;
        } else {
            return memcpy_repmov;
        }
    } else {
        if (processor::features().ssse3) {
            return memcpy_repmov_old_ssse3;
        } else {
            return memcpy_repmov_old;
        }
    }
}

void *memcpy(void *__restrict dest, const void *__restrict src, size_t n)
    __attribute__((ifunc("resolve_memcpy")));

// Since we are using explicit loops, and not the rep instruction
// (that requires a very specific rcx layout), we can use the same
// fixup for both versions here.
extern "C" void backwards_fixup(exception_frame *ef, size_t fixup)
{
    assert(fixup <= ef->rcx);
    ef->rdi -= fixup;
    ef->rsi -= fixup;
    ef->rcx -= fixup;
}

// In both the function below, we'll write the loop in C and the actual
// assignment in assembly, because it is a lot easier. But that means that the
// loop counter may be in some register other than rcx. Because we have listed
// it as an input operand, *at the time* of copy it will be in the correct
// position, which means the compiler will have to generate an extra operation
// in that scenario. We will trust the compiler to do the right thing and keep
// the counter in rcx since it knows it has to be there eventually. And if it
// can't, it can't. That's probably better than to code the whole thing ourselves.
static inline __always_inline void
byte_backwards(char * &d, const char * &s, size_t& n)
{
    // We could force a register and avoid declaring it, but it is better to leave
    // the compiler free.
    char tmp = 0;
    while (n--) {
        --d;
        --s;
        asm volatile
        (
         "1:\n\t"
         "mov (%1), %3\n\t"
         "mov %3, (%0)\n\t"
         ".pushsection .memcpy_decode, \"ax\" \n\t"
         ".quad 1b, 1, backwards_fixup\n\t"
         ".popsection\n"
            : "+D"(d), "+S"(s), "+c"(n) : "r"(tmp) : "memory");
    }
}

static inline __always_inline void
long_backwards(char * &d, const char * &s, size_t& n)
{
    unsigned long tmp = 0;
    for (; n >= sizeof(long); n -= sizeof(long)) {
        d -= sizeof(long);
        s -= sizeof(long);
        asm volatile
            (
             "1:\n\t"
             "mov    (%1), %3\n\t"
             "mov    %3, (%0)\n\t"
             ".pushsection .memcpy_decode, \"ax\" \n\t"
             ".quad 1b, 8, backwards_fixup\n\t"
             ".popsection\n"
                : "+D"(d), "+S"(s), "+c"(n) : "r"(tmp) : "memory");
    }
}

// According to Avi, this is likely to be faster than repmov with the direction
// flag set. Still, although always copying it byte by byte would be a lot simpler,
// it is faster to copy 8-byte aligned regions if we can. We'll go through the pain
// of doing that.
void *memcpy_backwards(void *dst, const void *src, size_t n)
{
    char *d = reinterpret_cast<char *>(dst);
    const char *s = reinterpret_cast<const char *>(src);

    // There are two fixup scenarios here to consider:
    // 1) If the addresses have the same alignment, in which case word-aligning one will
    // naturally word align the other
    // 2) mismatching alignments, in which we are no better if we copy words to the destination
    // since the source operand won't be word aligned.
    //
    // In general, we don't need to worry about fixups if the addresses are not aligned.
    // Since in the second case we will copy byte by byte anyway, we will do the whole operation
    // in one asm block.
    //
    // But for the first case, we can code it in C (without any fixups) until we reach the aligned
    // part, and only then introduce a fixup block.
    d += n;
    s += n;
    if ((uintptr_t)s % sizeof(unsigned long) == (uintptr_t)d % sizeof(unsigned long)) {
        while ((uintptr_t)(d) % sizeof(unsigned long)) {
            if (!n--) {
                return dst;
            }
            *(--d) = *(--s);
        }

        long_backwards(d, s, n);
    }

    byte_backwards(d, s, n);

    return dst;
}

extern memcpy_decoder memcpy_decode_start[], memcpy_decode_end[];

static void sort_memcpy_decoder() __attribute__((constructor(init_prio::sort)));

static void sort_memcpy_decoder()
{
    std::sort(memcpy_decode_start, memcpy_decode_end);
}

unsigned char *memcpy_decoder::dest(exception_frame *ef)
{
    return reinterpret_cast<unsigned char *>(ef->rdi);
}

unsigned char *memcpy_decoder::src(exception_frame *ef)
{
    return reinterpret_cast<unsigned char *>(ef->rsi);
}

size_t memcpy_decoder::size(exception_frame *ef)
{
    return ef->rcx * _size;
}

memcpy_decoder::memcpy_decoder(ulong pc, fixup_function fn)
    : _pc(pc), _fixup_fn(fn)
{
}

memcpy_decoder *memcpy_find_decoder(exception_frame *ef)
{
    memcpy_decoder v{ef->rip, 0};
    auto dec = std::lower_bound(memcpy_decode_start, memcpy_decode_end, v);
    if (dec != memcpy_decode_end && ((*dec) == ef->rip)) {
        return &*dec;
    }
    return nullptr;
}

static inline void small_memset(void *dest, int c, size_t n)
{
    size_t qty = n / 8;
    unsigned long *to_8 = (unsigned long *)dest;

    while (qty--) {
        *to_8++ = (uint8_t)c * 0x0101010101010101ull;
    }

    qty = n % 8;
    unsigned int *to_4 = (unsigned int *)to_8;

    if (qty / 4) {
        *to_4++ = (uint8_t)c * 0x01010101ul;
    }

    qty = qty % 4;
    unsigned short *to_2 = (unsigned short *)to_4;
    if (qty / 2) {
        *to_2++ = (uint8_t)c * 0x0101ul;
    }

    unsigned char *to = (unsigned char *)to_2;
    if (qty % 2) {
        *to++ = (uint8_t)c;
    }
}

extern "C"
void *memset_repstos_old(void *__restrict dest, int c, size_t n)
{
    auto ret = dest;
    if (n <= 64) {
        small_memset(dest, c, n);
    }
    else {
        auto nw = n / 8;
        auto nb = n & 7;
        auto cw = (uint8_t)c * 0x0101010101010101ull;
        asm volatile("rep stosq" : "+D"(dest), "+c"(nw) : "a"(cw) : "memory");
        asm volatile("rep stosb" : "+D"(dest), "+c"(nb) : "a"(cw) : "memory");
    }
    return ret;
}

extern "C"
void *memset_repstosb(void *__restrict dest, int c, size_t n)
{
    auto ret = dest;
    if (n <= 64) {
        small_memset(dest, c, n);
    } else {
        asm volatile("rep stosb" : "+D"(dest), "+c"(n) : "a"(c) : "memory");
    }
    return ret;
}

extern "C"
void *(*resolve_memset())(void *__restrict dest, int c, size_t n)
{
    if (processor::features().repmovsb) {
        return memset_repstosb;
    }
    return memset_repstos_old;
}

void *memset(void *__restrict dest, int c, size_t n)
    __attribute__((ifunc("resolve_memset")));


