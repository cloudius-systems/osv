/*
 * Copyright (C) 2015 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Tests for various fcntl() issues behave as they do on Linux.
//
// To compile on Linux, use: c++ -g -pthread -std=c++11 tst-fcntl.cc


#include <string>
#include <iostream>

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <sys/syscall.h>

static int tests = 0, fails = 0;

static void report(bool ok, std::string msg)
{
    ++tests;
    fails += !ok;
    std::cout << (ok ? "PASS" : "FAIL") << ": " << msg << "\n";
}

// This process holds no record lock that conflicts with its own query, so
// F_GETLK must answer F_UNLCK and leave the other fields unchanged.
static void check_getlk(int fd, struct flock *l, std::string where)
{
    l->l_type = F_WRLCK;
    l->l_whence = SEEK_SET;
    l->l_start = 10;
    l->l_len = 20;
    int r = fcntl(fd, F_GETLK, l);
    report(r == 0 && l->l_type == F_UNLCK, "F_GETLK reports no conflict, " + where);
    report(l->l_whence == SEEK_SET && l->l_start == 10 && l->l_len == 20,
           "F_GETLK leaves the range unchanged, " + where);
}

static struct flock static_lock;

int main(int ac, char** av)
{
    int fd = open("/tmp/tst-fcntl", O_CREAT | O_RDWR, 0777);
    report(fd > 0, "open");

    // GETFL on a default opened file should return the O_RDWR, plus
    // 0100000 (O_LARGEFILE, although this macro doesn't work).
    // The latter is not required by POSIX, but is returned on 64-bit
    // Linux, and allowed by POSIX (as explained in the Linux Standard
    // Base documentation for fcntl.
    int r = fcntl(fd, F_GETFL);
    report((r & O_ACCMODE) == O_RDWR, "GETFL access mode");
    report((r & ~O_ACCMODE) == 0100000, "GETFL rest");
    int save_r = r;

    // Set with F_SETFL the O_NONBLOCK mode
    r = fcntl(fd, F_SETFL, save_r | O_NONBLOCK);
    report(r == 0, "F_SETFL O_NONBLOCK");
    r = fcntl(fd, F_GETFL);
    report(r == (save_r | O_NONBLOCK), "F_GETFL");
    save_r = r;

    // GETFD on a default opened file should return 0
    r = fcntl(fd, F_GETFD);
    report(r == 0, "GETFD default");


    // SETFD with close on exec
    r = fcntl(fd, F_SETFD, FD_CLOEXEC);
    report(r == 0, "SETFD FD_CLOEXEC");

    // GETFD should now return FD_CLOEXEC
    r = fcntl(fd, F_GETFD);
    report(r == FD_CLOEXEC, "GETFD");

    // GETFL should be unchanged by this setting of SETFD
    r = fcntl(fd, F_GETFL);
    report(r == save_r, "GETFL");

    // dup() should have the same GETFL, but different GETFD
    // (back to the default)
    int dupfd = dup(fd);
    r = fcntl(dupfd, F_GETFL);
    report(r == save_r, "GETFL dup");
#if 0
    // Unfortunately, the following test does not pass on OSv, because it
    // currently makes FD_CLOEXEC per open file, instead of per-fd.
    r = fcntl(dupfd, F_GETFD);
    report(r == 0, "GETFD dup");
#endif
    close(dupfd);

    // O_CLOEXEC is *not* a file bit. We shouldn't be able to set
    // it with F_SETFL (Linux just ignores it), but when we get the
    // flags with F_GETFL, we won't see this bogus bit.
    r = fcntl(fd, F_SETFL, save_r | O_CLOEXEC);
    report(r == 0, "SETFL bogus");
    r = fcntl(fd, F_GETFL);
    report(r == save_r, "GETFL bogus");

    // fcntl() is variadic and reads its third argument according to the
    // command. Integer commands must see the value passed.
    int dfd = fcntl(fd, F_DUPFD, 100);
    report(dfd >= 100, "F_DUPFD honors the minimum descriptor");
    report(fcntl(dfd, F_GETFL) == save_r, "F_DUPFD duplicates the file");
    close(dfd);
    dfd = fcntl(fd, F_DUPFD_CLOEXEC, 200);
    report(dfd >= 200, "F_DUPFD_CLOEXEC honors the minimum descriptor");
    report(fcntl(dfd, F_GETFL) == save_r, "F_DUPFD_CLOEXEC duplicates the file");
    close(dfd);
    report(fcntl(fd, F_SETOWN, getpid()) == 0, "F_SETOWN");

    // Commands that take no argument give the same answer whether or not
    // the caller passes a spurious one.
    report(fcntl(fd, F_GETFD) == fcntl(fd, F_GETFD, 12345), "F_GETFD ignores extra argument");
    report(fcntl(fd, F_GETFL) == fcntl(fd, F_GETFL, 12345), "F_GETFL ignores extra argument");

    // Unknown commands fail with EINVAL, with or without an argument.
    errno = 0;
    report(fcntl(fd, 0x7fff) == -1 && errno == EINVAL, "unknown command");
    errno = 0;
    report(fcntl(fd, 0x7fff, 12345) == -1 && errno == EINVAL, "unknown command with argument");

    // The system call entry passes the same arguments on to fcntl().
    dfd = syscall(SYS_fcntl, fd, F_DUPFD, 300);
    report(dfd >= 300, "syscall F_DUPFD honors the minimum descriptor");
    close(dfd);
    report(syscall(SYS_fcntl, fd, F_GETFL) == save_r, "syscall F_GETFL");

    // F_GETLK writes through the caller's pointer, wherever it lives.
    struct flock stack_lock;
    struct flock *heap_lock = new struct flock;
    check_getlk(fd, &stack_lock, "stack");
    check_getlk(fd, heap_lock, "heap");
    check_getlk(fd, &static_lock, "static");
    report((uintptr_t)&stack_lock > UINT32_MAX || (uintptr_t)heap_lock > UINT32_MAX,
           "a struct flock above 4 GiB was exercised");
    delete heap_lock;

    stack_lock.l_type = F_RDLCK;
    report(fcntl(fd, F_GETLK, &stack_lock) == 0 && stack_lock.l_type == F_UNLCK,
           "F_GETLK F_RDLCK reports no conflict");
    errno = 0;
    report(lockf(fd, F_TEST, 0) == 0, "lockf F_TEST finds no conflicting lock");

    // As on Linux, the query must be F_RDLCK or F_WRLCK.
    stack_lock.l_type = 42;
    errno = 0;
    report(fcntl(fd, F_GETLK, &stack_lock) == -1 && errno == EINVAL && stack_lock.l_type == 42,
           "F_GETLK invalid l_type");
    stack_lock.l_type = F_UNLCK;
    errno = 0;
    report(fcntl(fd, F_GETLK, &stack_lock) == -1 && errno == EINVAL,
           "F_GETLK F_UNLCK query");
    errno = 0;
    report(fcntl(fd, F_GETLK, (struct flock *)nullptr) == -1 && errno == EFAULT,
           "F_GETLK null pointer");

    // The same query through syscall(), which glibc under the Linux dynamic
    // linker and static executables use, must see the whole pointer.
    stack_lock.l_type = F_WRLCK;
    report(syscall(SYS_fcntl, fd, F_GETLK, &stack_lock) == 0 && stack_lock.l_type == F_UNLCK,
           "syscall F_GETLK reports no conflict, stack");
    heap_lock = new struct flock;
    heap_lock->l_type = F_RDLCK;
    report(syscall(SYS_fcntl, fd, F_GETLK, heap_lock) == 0 && heap_lock->l_type == F_UNLCK,
           "syscall F_GETLK reports no conflict, heap");
    delete heap_lock;

    close(fd);
    remove("/tmp/tst-fcntl");

    report(fcntl(fd, F_GETFL) == -1 && errno == EBADF, "fcntl on closed fd");


    std::cout << "SUMMARY: " << tests << " tests, " << fails << " failures\n";
    return fails;
}


