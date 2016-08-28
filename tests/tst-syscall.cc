/*
 * Copyright (C) 2016 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <syscall.h>
#include <unistd.h>
#include <errno.h>

#include <iostream>

extern "C" long gettid();

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
    // Test that the x86 SYSCALL instruction works, and produces the same
    // results as the syscall() function (with expected differences in how
    // errors are returned).
    unsigned long syscall_nr = __NR_gettid;
    long tid = 0;

    asm ("movq %1, %%rax\n"
         "syscall\n"
	 "movq %%rax, %0\n"
         : "=m" (tid)
         : "m" (syscall_nr)
         : "rax", "rdi");

    std::cout << "got tid=" << tid << std::endl;
    expect(tid >= 0, true);
    expect(tid, gettid());

    // test that unknown system call results in a ENOSYS (see issue #757)
    expect_errno_l(syscall(999), ENOSYS);

    std::cout << "SUMMARY: " << tests << " tests, " << fails << " failures\n";
    return fails == 0 ? 0 : 1;
}
