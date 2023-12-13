/*
 * Copyright (C) 2016 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <syscall.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <cassert>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

#include <iostream>

extern "C" pid_t gettid();

static int tests = 0, fails = 0;

#define expect(actual, expected) do_expect(actual, expected, #actual, #expected, __FILE__, __LINE__)
template<typename T>
bool do_expect(T actual, T expected, const char *actuals, const char *expecteds, const char *file, int line)
{
    ++tests;
    if (actual != expected) {
        fails++;
        std::cout << "FAIL: " << file << ":" << line << ": For " << actuals <<
                ", expected " << expecteds << ", saw " << actual << ".\n";
        return false;
    }
    return true;
}

#define expect_errno_l(call, experrno) ( \
        do_expect(call, -1L, #call, "-1", __FILE__, __LINE__) && \
        do_expect(errno, experrno, #call " errno",  #experrno, __FILE__, __LINE__) )

int main(int argc, char **argv)
{
    // Test that the x86 SYSCALL and aarch64 SVC instructions work, and produce the same
    // results as the syscall() function (with expected differences in how
    // errors are returned).
    unsigned long syscall_nr = __NR_gettid;
    pid_t tid = 0;

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

    std::cout << "got tid=" << tid << std::endl;
    expect(tid >= 0, true);
    expect(tid, gettid());

    // test mmap as it takes 6 parameters
    int fd = open("/libenviron.so", O_RDONLY, 0666);
    assert(fd > 0);

    void *addr = NULL;
    size_t length = 8192;
    int prot = PROT_READ;
    int flags = MAP_PRIVATE;
    off_t offset = 0;
    void* buf = NULL;

    syscall_nr = __NR_mmap;

#ifdef __x86_64__
    asm ("movq %[addr], %%rdi\n"
         "movq %[length], %%rsi\n"
         "movl %[prot], %%edx\n"
         "movq %[flags], %%r10\n"
         "movq %[fd], %%r8\n"
         "movq %[offset], %%r9\n"
         "movq %[syscall_no], %%rax\n"
         "syscall\n"
         "movq %%rax, %[buf]\n"
         : [buf] "=m" (buf)
         : [addr] "m" (addr), [length] "m" (length), [prot] "m" (prot),
           [flags] "m" (flags), [fd] "m" (fd), [offset] "m" (offset), [syscall_no] "m" (syscall_nr));
#endif

#ifdef __aarch64__
    asm ("mov x0, %[addr]\n"
         "mov x1, %[length]\n"
         "mov x2, %[prot]\n"
         "mov x3, %[flags]\n"
         "mov x4, %[fd]\n"
         "mov x5, %[offset]\n"
         "mov x8, %[syscall_no]\n"
         "svc #0\n"
         "mov %[buf], x0\n"
         : [buf] "=r" (buf)
         : [addr] "r" (addr), [length] "r" (length), [prot] "r" (prot),
           [flags] "r" (flags), [fd] "r" (fd), [offset] "r" (offset), [syscall_no] "r" (syscall_nr)
         : "x0", "x1", "x2", "x3", "x4", "x5", "x8");
#endif

    assert(((long)buf) >= 0);
    munmap(buf, length);

    assert(close(fd) == 0);

    assert(chdir("/proc") == 0);
    unsigned long size = 4096;
    char path[size];
    assert(syscall(__NR_getcwd, path, size) == 6);
    assert(strcmp("/proc", path) == 0);

    // test that unknown system call results in a ENOSYS (see issue #757)
    expect_errno_l(syscall(999), ENOSYS);

    std::cout << "SUMMARY: " << tests << " tests, " << fails << " failures\n";
    return fails == 0 ? 0 : 1;
}
