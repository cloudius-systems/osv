#ifndef ALIGN_HH_
#define ALIGN_HH_

#include <osv/types.h>
#include <stddef.h>

template <typename T>
T align_down(T n, T alignment)
{
    return n & ~(alignment - 1);
}

template <typename T>
T align_up(T n, T alignment)
{
    return align_down(n + alignment - 1, alignment);
}

template <class T>
T* align_down(T* ptr, size_t alignment)
{
    auto n = reinterpret_cast<uintptr_t>(ptr);
    n = align_down(n, alignment);
    return reinterpret_cast<T*>(n);
}

template <class T>
T* align_up(T* ptr, size_t alignment)
{
    auto n = reinterpret_cast<uintptr_t>(ptr);
    n = align_up(n, alignment);
    return reinterpret_cast<T*>(n);
}

#endif
