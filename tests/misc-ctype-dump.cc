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

#include <clocale>
#include <cstdio>
#include <wchar.h>
#include <wctype.h>

static const char *const names[] = {
    "alnum", "alpha", "blank", "cntrl", "digit", "graph",
    "lower", "print", "punct", "space", "upper", "xdigit",
};

int main(int argc, char **argv)
{
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
    for (unsigned cp = 0; cp <= 0x10ffff; cp++) {
        unsigned bits = 0;
        for (int i = 0; i < 12; i++) {
            bits |= (iswctype(cp, t[i]) != 0) << i;
        }
        printf("%x %x %x %x %d\n", cp, bits, unsigned(towupper(cp)), unsigned(towlower(cp)),
               wcwidth(cp));
    }
    return fflush(stdout) ? 1 : 0;
}
