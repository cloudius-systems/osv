/*
 * Copyright (C) 2023 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <syscall.h>
#include <assert.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>

#ifdef __OSV__
long gettid();
#endif

void call_gettid_syscall()
{
    // errors are returned).
    unsigned long syscall_nr = __NR_gettid;
    long tid = 0;

#ifdef __x86_64__
    asm ("movq %[syscall_no], %%rax\n"
         "syscall\n"
         "movq %%rax, %[tid]\n"
         : [tid]"=m" (tid)
         : [syscall_no]"m" (syscall_nr)
         : "rax", "rdi");
#endif

#ifdef __aarch64__
    asm ("mov x8, %[syscall_no]\n"
         "svc #0\n"
         "mov %[tid], x0\n"
         : [tid]"=r" (tid)
         : [syscall_no]"r" (syscall_nr)
         : "x0", "x8");
#endif
    assert(tid >= 0);
}

uint64_t nstime()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint64_t mul = 1000000000, mul2 = 1000;
    return tv.tv_sec * mul + tv.tv_usec * mul2;
}

int main(int argc, char **argv)
{
    long count = 50000000;
    long loop = count;
    uint64_t start = nstime();

    while (loop--) {
        call_gettid_syscall();
    }

    uint64_t end = nstime();

    long average_syscall_duration = (end - start) / count;
    printf("%lu ns (elapsed %.2f sec) %s\n", average_syscall_duration, (end - start) / 1000000000.0, ": average gettid syscall duration");

#ifdef __OSV__
    loop = count;
    start = nstime();

    while (loop--) {
        long tid = gettid();
        assert(tid >= 0);
    }

    end = nstime();

    long average_local_gettid_duration = (end - start) / count;
    printf("%lu ns (elapsed %.2f sec) %s\n", average_local_gettid_duration, (end - start) / 1000000000.0, ": average gettid local duration");

    printf("Average syscall overhead: %lu ns\n", average_syscall_duration - average_local_gettid_duration);
#endif
}
