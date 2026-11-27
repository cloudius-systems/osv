/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

/*
 * Dump the wide-character classification, case mapping and width tables of
 * the C library this is built against, for every code point 0..0x10FFFF in
 * one locale. Build and run it on Linux (glibc with its standard C.utf8):
 *
 *   cc -O2 -o gen-ctype-oracle gen-ctype-oracle.c
 *   ./gen-ctype-oracle C.UTF-8 | ./ctype-summary.py > ctype-C.UTF-8.txt
 *
 * One line per code point: "<cp> <classes> <towupper> <towlower> <wcwidth>",
 * hex except the width; <classes> has bit i set when iswctype(cp,
 * wctype(names[i])) is true, in the order of names[] below.
 */
#define _XOPEN_SOURCE 700
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <wctype.h>

static const char *const names[] = {
    "alnum", "alpha", "blank", "cntrl", "digit", "graph",
    "lower", "print", "punct", "space", "upper", "xdigit",
};

int main(int argc, char **argv)
{
    if (argc != 2 || !setlocale(LC_ALL, argv[1])) {
        fprintf(stderr, "usage: %s <locale> (the locale must exist)\n", argv[0]);
        return 2;
    }
    wctype_t t[12];
    for (int i = 0; i < 12; i++) {
        t[i] = wctype(names[i]);
    }
    for (unsigned cp = 0; cp <= 0x10ffff; cp++) {
        unsigned bits = 0;
        for (int i = 0; i < 12; i++) {
            bits |= (iswctype(cp, t[i]) != 0) << i;
        }
        printf("%x %x %x %x %d\n", cp, bits, (unsigned)towupper(cp),
               (unsigned)towlower(cp), wcwidth(cp));
    }
    return 0;
}
