/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
/* glibc probe: the LC_CTYPE outdigit items and LC_NUMERIC wide items, and
 * printf width with a multibyte decimal point.  probe5 <locale>... */
#define _GNU_SOURCE
#include <langinfo.h>
#include <locale.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
static void show(const char *s)
{
    putchar('[');
    for (const unsigned char *p = (const unsigned char *)s; *p; p++)
        if (*p < 32 || *p > 126) printf("\\x%02x", *p); else putchar(*p);
    putchar(']');
}
int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (!setlocale(LC_ALL, argv[i])) { printf("%s: no\n", argv[i]); continue; }
        printf("== %s outdigit mb:", argv[i]);
        for (int d = 0; d < 10; d++) show(nl_langinfo(_NL_CTYPE_OUTDIGIT0_MB + d));
        printf(" wc:");
        for (int d = 0; d < 10; d++) printf(" %x", (unsigned)(uintptr_t)nl_langinfo(_NL_CTYPE_OUTDIGIT0_WC + d));
        printf(" dpwc %x tswc %x\n", (unsigned)(uintptr_t)nl_langinfo(_NL_NUMERIC_DECIMAL_POINT_WC),
               (unsigned)(uintptr_t)nl_langinfo(_NL_NUMERIC_THOUSANDS_SEP_WC));
        char b[200];
        static const char *ff[] = { "%12f|", "%012f", "%'14f|", "%'014f", "%12e|", "%12a|", "%12g|", "%I12f|", "%I12d|", "%'#o", "%'#.0o", "%'.0d", "%'p", "%'#X", "%Io", "%IX" };
        for (unsigned k = 0; k < sizeof ff / sizeof *ff; k++) {
            if (strchr("doXp", ff[k][strlen(ff[k]) - 1]) || strstr(ff[k], "d|")) snprintf(b, sizeof b, ff[k], 1234567);
            else snprintf(b, sizeof b, ff[k], 12345.25);
            printf("  %-7s %zu", ff[k], strlen(b)); show(b); printf("\n");
        }
    }
}
