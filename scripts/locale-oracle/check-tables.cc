/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Host check that libc/locale/ctype-utf8.cc reproduces the measured glibc
// tables exactly:
//
//   g++ -O2 -o check-tables check-tables.cc ../../libc/locale/ctype-utf8.cc
//   ./check-tables ctype | sha256sum      # == sha256 of d-glibc-C.UTF-8.txt
//   ./check-tables translit | sha256sum   # == grep -v ^ferror= tr-glibc-C.txt
//
// "ctype" prints gen-ctype-oracle.c's format for every code point, as if run
// in C.UTF-8; "translit" prints tr.c's format (without its ferror line).

#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <initializer_list>

extern "C" {
int __osv_utf8_iswctype(wint_t, int);
wint_t __osv_utf8_towupper(wint_t);
wint_t __osv_utf8_towlower(wint_t);
int __osv_utf8_wcwidth(wchar_t);
bool __osv_c_translit(wchar_t, const char**, size_t*);
}

int main(int argc, char** argv)
{
    if (argc == 2 && !strcmp(argv[1], "ctype")) {
        for (unsigned cp = 0; cp <= 0x10ffff; cp++) {
            unsigned bits = 0;
            for (int i = 0; i < 12; i++) {
                bits |= (__osv_utf8_iswctype(cp, i + 1) != 0) << i;
            }
            printf("%x %x %x %x %d\n", cp, bits, (unsigned)__osv_utf8_towupper(cp),
                (unsigned)__osv_utf8_towlower(cp), __osv_utf8_wcwidth(cp));
        }
        // Out of range: glibc gives no class, identity and width -1.
        for (unsigned cp : {0x110000u, 0x7fffffffu, 0xffffffffu}) {
            for (int i = 0; i <= 13; i++) {
                if (__osv_utf8_iswctype(cp, i) || __osv_utf8_towupper(cp) != cp ||
                    __osv_utf8_towlower(cp) != cp || __osv_utf8_wcwidth(cp) != -1) {
                    fprintf(stderr, "out-of-range mismatch at %x\n", cp);
                    return 1;
                }
            }
        }
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "translit")) {
        for (unsigned cp = 0x80; cp <= 0x10ffff; cp++) {
            const char* s;
            size_t n;
            if (__osv_c_translit(cp, &s, &n)) {
                printf("%x ", cp);
                for (size_t i = 0; i < n; i++) {
                    printf("%02x", (unsigned char)s[i]);
                }
                printf("\n");
            }
        }
        const char* s;
        size_t n;
        if (__osv_c_translit(-1, &s, &n) || __osv_c_translit(0x110000, &s, &n)) {
            fprintf(stderr, "out-of-range translit entry\n");
            return 1;
        }
        return 0;
    }
    fprintf(stderr, "usage: %s ctype|translit\n", argv[0]);
    return 2;
}
