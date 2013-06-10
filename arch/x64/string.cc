
#include <string.h>
#include "cpuid.hh"

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
    if (processor::features().repmovsb) {
        return memcpy_repmov;
    }
    return memcpy_base;
}

void *memcpy(void *__restrict dest, const void *__restrict src, size_t n)
    __attribute__((ifunc("resolve_memcpy")));
