/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Print the wide-character classes, case mappings and widths of every code
// point 0..0x10ffff in the locale named by argv[1] (default C), in the
// format of scripts/locale-oracle/gen-ctype-oracle.c, so its sha256 can be
// compared with the glibc dumps (scripts/locale-oracle/README.md):
//   "<cp> <classes> <towupper> <towlower> <wcwidth>", hex except the width;
//   <classes> has bit i set when iswctype(cp, wctype(names[i])).
// misc-ctype-dump --sha256 <name> prints only "sha256 <name> <hex>" of that
// output (the same as sha256sum of the plain dump).

#include <clocale>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <wchar.h>
#include <wctype.h>

// --sha256: print only the SHA-256 of the dump (FIPS 180-4), so a 25 MB dump
// need not cross a slow console.
namespace {

struct sha256 {
    uint32_t h[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    unsigned char buf[64];
    size_t n = 0;
    uint64_t len = 0;

    static uint32_t ror(uint32_t x, int r) { return x >> r | x << (32 - r); }

    void block(const unsigned char *p)
    {
        static const uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
        };
        uint32_t w[64];
        for (int i = 0; i < 16; i++) {
            w[i] = uint32_t(p[4 * i]) << 24 | p[4 * i + 1] << 16 | p[4 * i + 2] << 8 | p[4 * i + 3];
        }
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ w[i - 15] >> 3;
            uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ w[i - 2] >> 10;
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; i++) {
            uint32_t t1 = hh + (ror(e, 6) ^ ror(e, 11) ^ ror(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
            uint32_t t2 = (ror(a, 2) ^ ror(a, 13) ^ ror(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    void add(const char *p, size_t k)
    {
        len += k;
        while (k--) {
            buf[n++] = *p++;
            if (n == 64) {
                block(buf);
                n = 0;
            }
        }
    }

    void hex(char out[65])
    {
        uint64_t bits = len * 8;
        unsigned char one = 0x80, zero = 0;
        add(reinterpret_cast<const char *>(&one), 1);
        while (n != 56) {
            add(reinterpret_cast<const char *>(&zero), 1);
        }
        for (int i = 7; i >= 0; i--) {
            unsigned char c = bits >> (8 * i);
            add(reinterpret_cast<const char *>(&c), 1);
        }
        for (int i = 0; i < 8; i++) {
            snprintf(out + 8 * i, 9, "%08x", h[i]);
        }
    }
};

}

static const char *const names[] = {
    "alnum", "alpha", "blank", "cntrl", "digit", "graph",
    "lower", "print", "punct", "space", "upper", "xdigit",
};

int main(int argc, char **argv)
{
    bool hash = argc > 1 && !strcmp(argv[1], "--sha256");
    if (hash) {
        argc--;
        argv++;
    }
    const char *name = argc > 1 ? argv[1] : "C";
    if (!setlocale(LC_ALL, name)) {
        fprintf(stderr, "setlocale(%s) failed\n", name);
        return 2;
    }
    static char buf[1 << 16];
    setvbuf(stdout, buf, _IOFBF, sizeof(buf));
    wctype_t t[12];
    for (int i = 0; i < 12; i++) {
        t[i] = wctype(names[i]);
    }
    sha256 h;
    for (unsigned cp = 0; cp <= 0x10ffff; cp++) {
        unsigned bits = 0;
        for (int i = 0; i < 12; i++) {
            bits |= (iswctype(cp, t[i]) != 0) << i;
        }
        char line[64];
        int k = snprintf(line, sizeof(line), "%x %x %x %x %d\n", cp, bits, unsigned(towupper(cp)),
                         unsigned(towlower(cp)), wcwidth(cp));
        if (hash) {
            h.add(line, k);
        } else {
            fwrite(line, 1, k, stdout);
        }
    }
    if (hash) {
        char out[65];
        h.hex(out);
        printf("sha256 %s %s\n", name, out);
    }
    return fflush(stdout) ? 1 : 0;
}
