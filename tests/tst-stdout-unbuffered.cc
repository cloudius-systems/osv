/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// After setvbuf(stdout, NULL, _IONBF, 0), stdout is unbuffered: each write
// must reach file descriptor 1 at once, with nothing left in the stream's
// buffer.  This test writes through puts(), fputs(), fwrite() and fputc()
// in turn, and after each one checks with __fpending() that no bytes are
// pending.  On OSv the kernel shares stdout with the application and has
// written to it before main(), so setvbuf() is not the first operation on
// the stream; the test also checks that switching stdout between _IOFBF
// and _IOLBF after output first writes out what was pending, keeping the
// bytes in order.  Last, it checks setvbuf() as the first operation on a
// freshly opened file, the one case the C standard defines.
//
// The results go to stderr through write(), so they do not depend on the
// stream under test.

#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <stdio_ext.h>
#include <string.h>
#include <unistd.h>

static int failures;

static void report(bool ok, const char *what, size_t pending)
{
    char line[160];
    int n = snprintf(line, sizeof line, "%s: %s (%zu bytes pending)\n",
                     ok ? "PASS" : "FAIL", what, pending);
    write(2, line, n);
    if (!ok) {
        failures++;
    }
}

// Returns the size of the file open on fd, or -1.
static off_t file_size(int fd)
{
    struct stat st;
    return fstat(fd, &st) == 0 ? st.st_size : -1;
}

// Switches stdout between _IOFBF and _IOLBF after it has pending output.
// File descriptor 1 is pointed at a file for the duration, so the bytes
// that reach it, and their order, can be read back.
static void test_switch_after_output()
{
    const char *path = "/tmp/tst-stdout-unbuffered.out";
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    int saved = dup(1);
    if (fd < 0 || saved < 0) {
        report(false, "open the output file and save fd 1", 0);
        return;
    }
    fflush(stdout);
    dup2(fd, 1);

    // The stream's own buffer is kept (buf is NULL), so nothing here
    // depends on a buffer owned by this test.
    fputs("a", stdout);
    setvbuf(stdout, nullptr, _IOFBF, 0);
    size_t p = __fpending(stdout);
    report(p == 0 && file_size(fd) == 1,
           "setvbuf(_IOFBF) writes out the output pending before it", p);

    fputs("b\n", stdout);
    p = __fpending(stdout);
    report(p == 2, "after setvbuf(_IOFBF), a full line stays buffered", p);

    setvbuf(stdout, nullptr, _IOLBF, 0);
    p = __fpending(stdout);
    report(p == 0 && file_size(fd) == 3,
           "setvbuf(_IOLBF) writes out the output pending before it", p);

    fputs("c", stdout);
    fputs("\n", stdout);
    p = __fpending(stdout);
    report(p == 0, "after setvbuf(_IOLBF), a newline writes the line", p);

    // No fflush() here: it would clear the stream's write state, and the
    // _IONBF checks in main() need a stream that has been written to.
    dup2(saved, 1);
    close(saved);

    char got[16] = {};
    ssize_t n = pread(fd, got, sizeof got - 1, 0);
    report(n == 5 && memcmp(got, "ab\nc\n", 5) == 0,
           "the bytes reach the file in the order they were written",
           n < 0 ? 0 : n);
    close(fd);
    unlink(path);
}

// setvbuf() as the first operation on a freshly opened file.
static void test_fresh_file()
{
    const char *path = "/tmp/tst-stdout-unbuffered.fresh";
    FILE *f = fopen(path, "w");
    if (!f) {
        report(false, "fopen() a fresh file", 0);
        return;
    }
    static char buf[4096];
    setvbuf(f, buf, _IOFBF, sizeof buf);
    fputs("0123456789\n", f);
    size_t p = __fpending(f);
    report(p == 11 && file_size(fileno(f)) == 0,
           "a fresh file with setvbuf(_IOFBF) buffers its output", p);
    fflush(f);
    report(file_size(fileno(f)) == 11, "fflush() writes the buffered output",
           __fpending(f));
    fclose(f);

    f = fopen(path, "w");
    if (!f) {
        report(false, "fopen() a fresh file", 0);
        return;
    }
    setvbuf(f, nullptr, _IONBF, 0);
    fputs("0123456789\n", f);
    p = __fpending(f);
    report(p == 0 && file_size(fileno(f)) == 11,
           "a fresh file with setvbuf(_IONBF) writes at once", p);
    fclose(f);
    unlink(path);
}

int main()
{
    test_switch_after_output();
    test_fresh_file();

    setvbuf(stdout, nullptr, _IONBF, 0);

    puts("tst-stdout-unbuffered: line written with puts()");
    size_t p = __fpending(stdout);
    report(p == 0, "puts() on an unbuffered stdout leaves nothing pending", p);

    fputs("tst-stdout-unbuffered: line written with fputs()\n", stdout);
    p = __fpending(stdout);
    report(p == 0, "fputs() on an unbuffered stdout leaves nothing pending", p);

    const char w[] = "tst-stdout-unbuffered: line written with fwrite()\n";
    fwrite(w, 1, strlen(w), stdout);
    p = __fpending(stdout);
    report(p == 0, "fwrite() on an unbuffered stdout leaves nothing pending", p);

    fputc('\n', stdout);
    p = __fpending(stdout);
    report(p == 0, "fputc() on an unbuffered stdout leaves nothing pending", p);

    char line[64];
    int n = snprintf(line, sizeof line, "SUMMARY: %d failures\n", failures);
    write(2, line, n);
    return failures ? 1 : 0;
}
