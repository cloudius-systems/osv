/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
/* Host differential: glibc's strfmon_l against OSv's libc/locale/strfmon.c
 * built for Linux (build.sh) as osv_strfmon_l, same compiled data (LOCPATH):
 *   fmd <locale>...   prints the cases that differ and a SUMMARY; exit 1 if any.
 * Formats: 16 flag sets x 6 widths x 5 left x 5 right precisions x n/i, on
 * 14 values; error formats; buffer sizes 0..40; 1e308 (311 digits, which
 * with fr_FR's 3-byte separator is 618 bytes) at buffer sizes around it. */
#define _GNU_SOURCE
#include <errno.h>
#include <locale.h>
#include <monetary.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
ssize_t osv_strfmon_l(char *, size_t, locale_t, const char *, ...);
static long ndiff, ntot;
static void cmp(const char *ln, locale_t l, size_t n, const char *f, double v)
{
    char a[8192], b[8192];
    memset(a, 0x55, sizeof a);
    memset(b, 0x55, sizeof b);
    errno = 0;
    ssize_t ra = strfmon_l(a, n, l, f, v, v);
    int ea = errno;
    errno = 0;
    ssize_t rb = osv_strfmon_l(b, n, l, f, v, v);
    int eb = errno;
    ntot++;
    if (ra != rb || (ra < 0 && ea != eb) || (ra >= 0 && memcmp(a, b, ra + 1))) {
        ndiff++;
        if (ndiff < 300)
            printf("DIFF %s n=%zu [%s] %.17g glibc %zd/%d [%s] osv %zd/%d [%s]\n", ln, n, f, v, ra, ea,
                   ra >= 0 ? a : "", rb, eb, rb >= 0 ? b : "");
    }
}
int main(int argc, char **argv)
{
    static const char *flags[] = { "", "^", "!", "(", "+", "-", "=*", "=0", "^!", "(!", "-(", "=*^",
                                   "+!", "=*-", "-^!(", "^^" };
    static const char *widths[] = { "", "0", "1", "8", "14", "25" };
    static const char *lps[] = { "", "#0", "#1", "#5", "#10" };
    static const char *rps[] = { "", ".0", ".1", ".2", ".5" };
    static const double vals[] = { 0, 1, -1, 1.5, -0.5, 0.125, 2.675, 12.34, -1234.56, 1234567.891,
                                   -1234567.891, 1e15, -1e20, 123456789012.345 };
    static const char *bad[] = { "%", "%q", "%5", "%#n", "%=", "%=n", "%+(n", "%(+n", "%-5-n", "%.n",
                                 "%%", "a%%b%n", "%n|%i", "%#n", "%5.n", "%10#n", "%^#5.1i",
                                 "x%nx", "%=\x01n", "%-0n", "%0n", "%=%n", "%==n" };
    for (int li = 1; li < argc; li++) {
        locale_t l = newlocale(LC_ALL_MASK, argv[li], 0);
        if (!l) { printf("NOLOCALE %s\n", argv[li]); ndiff++; continue; }
        for (unsigned a = 0; a < sizeof flags / sizeof *flags; a++)
        for (unsigned b = 0; b < sizeof widths / sizeof *widths; b++)
        for (unsigned c = 0; c < sizeof lps / sizeof *lps; c++)
        for (unsigned d = 0; d < sizeof rps / sizeof *rps; d++)
        for (int e = 0; e < 2; e++) {
            char f[64];
            snprintf(f, sizeof f, "%%%s%s%s%s%c", flags[a], widths[b], lps[c], rps[d], e ? 'i' : 'n');
            for (unsigned v = 0; v < sizeof vals / sizeof *vals; v++) cmp(argv[li], l, 1024, f, vals[v]);
        }
        for (unsigned k = 0; k < sizeof bad / sizeof *bad; k++)
            for (unsigned v = 0; v < 3; v++) cmp(argv[li], l, 1024, bad[k], vals[v * 5]);
        for (size_t n = 0; n < 40; n++) cmp(argv[li], l, n, "%n|%#5i", -1234.56);
        for (unsigned a = 0; a < sizeof flags / sizeof *flags; a++)
            for (int e = 0; e < 2; e++) {
                char f[64];
                snprintf(f, sizeof f, "%%%s%c", flags[a], e ? 'i' : 'n');
                cmp(argv[li], l, 8192, f, 1e308);
                cmp(argv[li], l, 8192, f, -1e308);
            }
        for (size_t n = 300; n < 700; n++) cmp(argv[li], l, n, "%!n", 1e308);
        freelocale(l);
    }
    printf("SUMMARY %ld of %ld differ\n", ndiff, ntot);
    return ndiff != 0;
}
