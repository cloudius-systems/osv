
#include <string.h>
#include "processor.hh"

extern "C"
void *memcpy_base(void *__restrict dest, const void *__restrict src, size_t n);

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
    using processor::cpuid;
    if (cpuid(0).a >= 7 && (cpuid(7, 0).b & 0x200)) {
        return memcpy_repmov;
    }
    return memcpy_base;
}

void *memcpy(void *__restrict dest, const void *__restrict src, size_t n)
    __attribute__((ifunc("resolve_memcpy")));
