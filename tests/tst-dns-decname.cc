/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// __dns_get_rr() decodes a name from a DNS reply into a 256-byte buffer and
// then strcpy()s it out. The decoded name must always be NUL-terminated,
// including a name that ends in a compression pointer to the root label, and
// an empty name. Each case decodes into a buffer pre-filled with 'X', so a
// missing terminator shows up as a long or unbounded result.

#include <stddef.h>
#include <stdio.h>
#include <string.h>

// OSv-internal (libc/network/__dns.cc).
extern "C" int __dns_get_rr(void *, size_t, size_t, size_t, const unsigned char *, int, int);

#define RR_CNAME 5


static int tests, fails;

static void report(bool ok, const char *what)
{
    ++tests;
    fails += !ok;
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
}

// Leave 'X' in the stack below the caller, where __dns_get_rr()'s frame and
// its 256-byte name buffer will be, as after other calls in a real program.
__attribute__((noinline)) static void dirty_stack()
{
    char dirty[4096];
    memset(dirty, 'X', sizeof dirty);
    __asm__ volatile("" :: "r"(dirty) : "memory");
}

// One reply, no question, one CNAME answer whose rdata (at offset 23) is `rd`.
// If `extra` is given, its `en` bytes are also placed at offset `eoff` of the
// reply, after the answer, for the rdata to point to.
static int decode(const unsigned char *rd, int n, char *out,
                  const unsigned char *extra = nullptr, int eoff = 0, int en = 0)
{
    static unsigned char r[1024];
    memset(r, 0, sizeof r);
    r[7] = 1;                                   // ancount = 1
    unsigned char *p = r + 12;
    *p++ = 0;                                   // owner name: root (offset 12)
    *p++ = 0; *p++ = RR_CNAME;                  // type
    *p++ = 0; *p++ = 1;                         // class
    p += 4;                                     // ttl
    *p++ = 0; *p++ = n;                         // rdlength
    memcpy(p, rd, n);
    if (extra) {
        memcpy(r + eoff, extra, en);
    }
    // A 512-byte destination full of 'X': strcpy of an unterminated name
    // stops at the first NUL past the decoder's 256-byte buffer, or never.
    memset(out, 'X', 511);
    out[511] = 0;
    dirty_stack();
    return __dns_get_rr(out, 0, 256, 1, r, RR_CNAME, 1);
}

static void expect(const char *what, const unsigned char *rd, int n, const char *want,
                   const unsigned char *extra = nullptr, int eoff = 0, int en = 0)
{
    char out[512];
    int found = decode(rd, n, out, extra, eoff, en);
    bool ok = found == 1 && strnlen(out, sizeof out) == strlen(want) && !strcmp(out, want);
    if (!ok) {
        printf("  found=%d len=%zu\n", found, strnlen(out, sizeof out));
    }
    report(ok, what);
}

int main()
{
    const unsigned char plain[] = { 3, 'f', 'o', 'o', 3, 'b', 'a', 'r', 0 };
    expect("a plain name decodes as \"foo.bar\"", plain, sizeof plain, "foo.bar");

    // Label "a", then a pointer to offset 2 of the reply, which is 0 (root).
    const unsigned char ptr_root[] = { 1, 'a', 0xc0, 0x02 };
    expect("a name ending in a pointer to the root is terminated", ptr_root, sizeof ptr_root, "a");

    // Label "x", then a pointer to the owner name at offset 12 (root).
    const unsigned char ptr_owner[] = { 1, 'x', 0xc0, 0x0c };
    expect("a name ending in a pointer to a root owner is terminated", ptr_owner, sizeof ptr_owner, "x");

    const unsigned char empty[] = { 0 };
    expect("an empty name decodes as \"\"", empty, sizeof empty, "");

    // Label "a", then a pointer to "foo" at offset 300 (0x12c). A pointer's
    // offset is 14 bits, so offsets above 255 must work too.
    const unsigned char ptr_far[] = { 1, 'a', 0xc1, 0x2c };
    const unsigned char foo[] = { 3, 'f', 'o', 'o', 0 };
    expect("a name ending in a pointer to offset 300", ptr_far, sizeof ptr_far, "a.foo",
           foo, 300, sizeof foo);

    // A pointer to an offset beyond the 512-byte reply is rejected.
    const unsigned char ptr_out[] = { 1, 'a', 0xc2, 0x00 };
    char out[512];
    report(decode(ptr_out, sizeof ptr_out, out) == -1, "a pointer past the reply is rejected");

    // Label "hi", then a pointer back to the rdata itself (offset 23).
    const unsigned char loop[] = { 2, 'h', 'i', 0xc0, 0x17 };
    report(decode(loop, sizeof loop, out) == -1, "a pointer loop is rejected");

    printf("SUMMARY: %d tests, %d failures\n", tests, fails);
    return fails ? 1 : 0;
}
