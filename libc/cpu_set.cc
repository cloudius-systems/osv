/*
 * Copyright (C) 2015 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Some of the macros described in cpu_set(3), namely CPU_COUNT(), CPU_ALLOC()
// and CPU_FREE(), are implemented by glibc as calling hidden library
// functions, __sched_cpucount(), __sched_cpualloc() and __sched_cpufree
// respectively. We need to implement these functions, for applications
// which use those macros and were compiled with standard Linux header files.

#include <stddef.h>
#include <sched.h>
#include <stdlib.h>

extern "C"
int __sched_cpucount (size_t setsize, const cpu_set_t *cpuset)
{
    int count = 0;
    const __cpu_mask *p = cpuset->__bits;
    // Note that we assume, like glibc's implementation does, that setsize
    // is a multiple of sizeof(__cpu_mask). The CPU_ALLOC_SIZE() macro indeed
    // always returns a multiple of sizeof(__cpu_mask).
    const __cpu_mask *end = p + (setsize / sizeof(__cpu_mask));

    while (p < end) {
        count +=  __builtin_popcountl(*p++);
    }

    return count;
}

extern "C"
cpu_set_t *__sched_cpualloc (size_t count)
{
    return (cpu_set_t*) malloc (CPU_ALLOC_SIZE (count));
}

extern "C"
void __sched_cpufree (cpu_set_t *set)
{
    free (set);
}
