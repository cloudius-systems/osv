/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
// To compile on Linux, use: g++ -g -pthread -std=c++11 tests/tst-uio.cc

// This test tests for a bug we had with readv(), writev(), which modified
// the iovecs it got (zeroing their lengths) in complete violation of the
// const-ness of these structures. This bug also caused fread() to return
// garbage, so we also test fread() here.

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/uio.h>
#include <string.h>

#include <iostream>


static int tests = 0, fails = 0;

template<typename T>
bool do_expect(T actual, T expected, const char *actuals, const char *expecteds, const char *file, int line)
{
    ++tests;
    if (actual != expected) {
        fails++;
        std::cout << "FAIL: " << file << ":" << line << ": For " << actuals
                << " expected " << expecteds << "(" << expected << "), saw "
                << actual << ".\n";
        return false;
    }
    std::cout << "OK: " << file << ":" << line << ".\n";
    return true;
}
template<typename T>
bool do_expectge(T actual, T expected, const char *actuals, const char *expecteds, const char *file, int line)
{
    ++tests;
    if (actual < expected) {
        fails++;
        std::cout << "FAIL: " << file << ":" << line << ": For " << actuals
                << " expected >=" << expecteds << ", saw " << actual << ".\n";
        return false;
    }
    std::cout << "OK: " << file << ":" << line << ".\n";
    return true;
}
#define expect(actual, expected) do_expect(actual, expected, #actual, #expected, __FILE__, __LINE__)
#define expectge(actual, expected) do_expectge(actual, expected, #actual, #expected, __FILE__, __LINE__)
#define expect_errno(call, experrno) ( \
        do_expect((long)(call), (long)-1, #call, "-1", __FILE__, __LINE__) && \
        do_expect(errno, experrno, #call " errno",  #experrno, __FILE__, __LINE__) )
#define expect_success(var, call) \
        errno = 0; \
        var = call; \
        do_expectge(var, 0, #call, "0", __FILE__, __LINE__); \
        do_expect(errno, 0, #call " errno",  "0", __FILE__, __LINE__);

int main()
{
    // Test that readv() does *not* modify its iovec parameter while it
    // keeps track of how much it copied. Unbelievably, we had such a bug
    // in OSv and didn't notice it for over a year.
#ifdef __OSV__
    const char* fn = "/tests/tst-rename.so";   // A file roughly 200KB in size.
#else
    const char* fn = "build/release/tests/tst-rename.so";
#endif
    int fd;
    expect_success(fd, open(fn, O_RDONLY));
    char buf1[100], buf2[200];
    struct iovec iov[2] = {
        { .iov_base = buf1, .iov_len = sizeof(buf1) },
        { .iov_base = buf2, .iov_len = sizeof(buf2) }
    };
    int x;
    expect_success(x, (int)readv(fd, iov, 2));
    expect(x, (int)sizeof(buf1)+(int)sizeof(buf2));
    expect(iov[0].iov_len, sizeof(buf1));
    expect(iov[1].iov_len, sizeof(buf2));
    close(fd);

    // Test that writev() does not modify its iovec parameter.
    const char* tmp = "/tmp/tst-uio-tmp";
    expect_success(fd, open(tmp, O_WRONLY|O_CREAT, 0777));
    char s1[]={'y','o',' '}, s2[]={'m','a','n','\0'};
    struct iovec siov[2] = {
        { .iov_base = s1, .iov_len = sizeof(s1) },
        { .iov_base = s2, .iov_len = sizeof(s2) }
    };
    expect_success(x, (int)writev(fd, siov, 2));
    expect(x, 7);
    expect(siov[0].iov_len, sizeof(s1));
    expect(siov[1].iov_len, sizeof(s2));
    close(fd);
    expect_success(fd, open(tmp, O_RDONLY));
    expect(read(fd, buf1, sizeof(buf1)), (long)7);
    expect(strcmp(buf1, "yo man"), 0);
    close(fd);
    unlink(tmp);




    /////////////////////////////////////////////////////////////////////////
    // Test reading a file with getc(), fread() and read().
    // We used to have a bug with the fread() test, caused by the readv()
    // bug mentioned above.
    struct stat st;
    int i;
    expect_success(i, stat(fn, &st));
    std::cout << "File size: " << st.st_size << "\n";
    // Read the file with getc, character by character
    char *b1 = (char *)malloc(st.st_size);
    FILE *fp = fopen(fn, "r");
    for (i = 0; i < st.st_size; i++) {
        int c = getc(fp);
        assert(c >= 0); // not yet EOF
        b1[i] = c;
    }
    expect(getc(fp), EOF);
    fclose(fp);
    // Read the file with read(), with exponentially
    // growing chunks.
    fd = open(fn, O_RDONLY);
    unsigned int chunk = 1024;
    char *b2 = (char *)malloc(st.st_size);
    i = 0;
    while (true) {
        std::cout << "read() at " << i << " got ";
        auto nr = read(fd, b2 + i, chunk);
        std::cout << nr << ".\n";
        // compare read bytes to the ones read earlier with getc
        tests++;
        for (unsigned j = i; j < i + nr; j++) {
            if (b1[j] != b2[j]) {
                fails++;
                std::cout << "Mismatch at byte " << j << "\n";
                break;
            }
        }
        i += nr;
        if (nr < chunk) break;
        chunk *= 2; // use a bigger chunk next time
    }
    close(fd);
    free(b2);
    expect((long)i, st.st_size);
    // Read the file with fread(), with exponentially
    // growing chunks. This is Lua's "read_all" algorithm
    // (see http://www.lua.org/source/5.2/liolib.c.html#read_all).
    fp = fopen(fn, "r");
    chunk = 1024;
    b2 = (char *)malloc(st.st_size);
    i = 0;
    while (true) {
        std::cout << "fread() at " << i << " got ";
        auto nr = fread(b2 + i, 1, chunk, fp);
        std::cout << nr << ".\n";
        // compare read bytes to the ones read earlier with getc
        tests++;
        for (unsigned j = i; j < i + nr; j++) {
            if (b1[j] != b2[j]) {
                fails++;
                std::cout << "Mismatch at byte " << j << "\n";
                break;
            }
        }
        i += nr;
        if (nr < chunk) break;
        chunk *= 2; // use a bigger chunk next time
    }
    expect(getc(fp), EOF);
    fclose(fp);
    expect((long)i, st.st_size);


    free(b1);
    free(b2);

    std::cout << "SUMMARY: " << tests << " tests, " << fails << " failures\n";
    return fails == 0 ? 0 : 1;
}
