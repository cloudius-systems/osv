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

extern "C"
void *memcpy_base(void *__restrict dest, const void *__restrict src, size_t n);
extern "C"
void *memset_base(void *__restrict dest, int c, size_t n);

extern "C"
void *memcpy_repmov_old(void *__restrict dest, const void *__restrict src, size_t n)
{
    auto ret = dest;
    auto nw = n / 8;
    auto nb = n & 7;
    asm volatile("rep movsq" : "+D"(dest), "+S"(src), "+c"(nw) : : "memory");
    asm volatile("rep movsb" : "+D"(dest), "+S"(src), "+c"(nb) : : "memory");
    return ret;
}


extern "C"
void *memcpy_repmov(void *__restrict dest, const void *__restrict src, size_t n)
{
    auto ret = dest;
    asm volatile("rep movsb" : "+D"(dest), "+S"(src), "+c"(n) : : "memory");
    return ret;
}

extern "C"
void *(*resolve_memcpy())(void *__restrict dest, const void *__restrict src, size_t n)
{
    if (processor::features().repmovsb) {
        return memcpy_repmov;
    }
    return memcpy_repmov_old;
}

void *memcpy(void *__restrict dest, const void *__restrict src, size_t n)
    __attribute__((ifunc("resolve_memcpy")));

// According to Avi, this is likely to be faster than repmov with the direction
// flag set. Still, although always copying it byte by byte would be a lot simpler,
// it is faster to copy 8-byte aligned regions if we can. We'll go through the pain
// of doing that.
void *memcpy_backwards(void *dst, const void *src, size_t n)
{
    char *d = reinterpret_cast<char *>(dst);
    const char *s = reinterpret_cast<const char *>(src);

    if ((uintptr_t)s % sizeof(unsigned long) == (uintptr_t)d % sizeof(unsigned long)) {
        while ((uintptr_t)(d+n) % sizeof(unsigned long)) {
            if (!n--) {
                return dst;
            }
            d[n] = s[n];
        }

        while (n >= sizeof(unsigned long)) {
            n -= sizeof(unsigned long);
            *(unsigned long *)(d+n) = *(unsigned long *)(s+n);
        }
    }

    while (n--) {
        d[n] = s[n];
    }

    return dst;
}

extern "C"
void *memset_repstos_old(void *__restrict dest, int c, size_t n)
{
    auto ret = dest;
    auto nw = n / 8;
    auto nb = n & 7;
    auto cw = (uint8_t)c * 0x0101010101010101ull;
    asm volatile("rep stosq" : "+D"(dest), "+c"(nw) : "a"(cw) : "memory");
    asm volatile("rep stosb" : "+D"(dest), "+c"(nb) : "a"(cw) : "memory");
    return ret;
}

extern "C"
void *memset_repstosb(void *__restrict dest, int c, size_t n)
{
    auto ret = dest;
    asm volatile("rep stosb" : "+D"(dest), "+c"(n) : "a"(c) : "memory");
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


