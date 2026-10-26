/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
/* Host differential: glibc's strftime_l and wcsftime_l against OSv's
 * libc/time/strftime.c built for Linux (build.sh) as osv___strftime_l and
 * osv___wcsftime_l, with the same compiled locale data (LOCPATH):
 *   sfd <mode> <locale>...   mode g: print glibc's results; d: print the
 *   specs that differ and a SUMMARY line; exit 1 if any differ.
 * Every conversion character 32..126 with 17 flag sets, 9 widths and the
 * E/O modifiers, on 12 dates (years -5..12345, offsets -10 h..+5:30), and
 * buffer sizes 0..11 (return 0 when the result does not fit); 14 conversion
 * characters U+0080..U+10FFFF (not conversions) with 6 flag sets and 3 widths. */
#define _GNU_SOURCE
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>
#include <wctype.h>
size_t osv___strftime_l(char *, size_t, const char *, const struct tm *, locale_t);
size_t osv___wcsftime_l(wchar_t *, size_t, const wchar_t *, const struct tm *, locale_t);
static const char *flags[] = { "", "_", "-", "0", "^", "#", "^#", "#^", "_^", "0#", "-^", "_0", "0_", "-0", "0-", "^^", "__" };
static const char *widths[] = { "", "1", "2", "3", "4", "5", "7", "10", "25" };
static const char *mods[] = { "", "E", "O", "EO", "OE" };
static struct tm dates[16];
static int ndates;
static void add(int y, int mo, int d, int h, int mi, int s, long off, const char *zone)
{
    struct tm tm; memset(&tm, 0, sizeof tm);
    tm.tm_year = y - 1900; tm.tm_mon = mo - 1; tm.tm_mday = d; tm.tm_hour = h; tm.tm_min = mi; tm.tm_sec = s;
    time_t t = timegm(&tm); gmtime_r(&t, &tm);
    tm.tm_isdst = 0; tm.tm_gmtoff = off; tm.tm_zone = zone;
    dates[ndates++] = tm;
}
int main(int argc, char **argv)
{
    setlocale(LC_ALL, "C.UTF-8");
    char mode = argv[1][0];
    add(2026, 1, 1, 0, 0, 0, 0, "UTC"); add(2026, 7, 15, 13, 45, 30, 0, "UTC"); add(1999, 12, 31, 23, 59, 59, 0, "UTC");
    add(2019, 5, 3, 0, 7, 9, 19800, "IST"); add(1989, 1, 7, 12, 0, 0, -12600, "NST"); add(5, 1, 5, 9, 3, 1, 3600, "CET");
    add(999, 3, 9, 11, 1, 2, 0, "UTC"); add(12345, 6, 7, 18, 0, 0, 0, "UTC"); add(-5, 2, 3, 4, 5, 6, 0, "UTC");
    add(1, 1, 1, 0, 0, 0, 0, "UTC"); add(2020, 12, 31, 12, 0, 0, -36000, "HST"); add(1912, 7, 30, 1, 2, 3, 0, "UTC");
    long ndiff = 0, ntot = 0;
    for (int li = 2; li < argc; li++) {
        locale_t loc = newlocale(LC_ALL_MASK, argv[li], 0);
        if (!loc) { printf("NOLOCALE %s\n", argv[li]); continue; }
        for (int di = 0; di < ndates; di++)
        for (unsigned fi = 0; fi < sizeof flags / sizeof *flags; fi++)
        for (unsigned wi = 0; wi < sizeof widths / sizeof *widths; wi++)
        for (unsigned mi = 0; mi < sizeof mods / sizeof *mods; mi++)
        for (int c = 32; c < 128; c++) {
            if ((c >= '0' && c <= '9') || strchr("_-^#EO", c)) continue;
            if (c != 'Y' && c != 'd' && c != 'p' && c != 'a' && di >= 6 && (fi > 5 || wi > 4)) continue;
            char f[32]; snprintf(f, sizeof f, "<%%%s%s%s%c>", flags[fi], widths[wi], mods[mi], c == 127 ? 0 : c);
            if (c == 127) strcat(f, "");
            char a[1024], b[1024];
            memset(a, 0x55, sizeof a); memset(b, 0x55, sizeof b);
            size_t na = strftime_l(a, sizeof a, f, &dates[di], loc);
            size_t nb = osv___strftime_l(b, sizeof b, f, &dates[di], loc);
            ntot++;
            {
                wchar_t wf[64], wa[1024], wb[1024];
                mbstowcs(wf, f, 64);
                size_t ma = wcsftime_l(wa, 1024, wf, &dates[di], loc), mb = osv___wcsftime_l(wb, 1024, wf, &dates[di], loc);
                if (ma != mb || (ma && wmemcmp(wa, wb, ma + 1))) {
                    ndiff++;
                    if (ndiff < 400) printf("WDIFF %s d%d %s glibc %zu [%ls] osv %zu [%ls]\n", argv[li], di, f, ma, ma ? wa : L"", mb, mb ? wb : L"");
                }
                for (size_t k = 0; k < 12 && di < 3 && wi == 7 && fi < 6; k++) {
                    ma = wcsftime_l(wa, k, wf, &dates[di], loc); mb = osv___wcsftime_l(wb, k, wf, &dates[di], loc);
                    if (ma != mb || (ma && wmemcmp(wa, wb, ma + 1))) { ndiff++; printf("WSMALL %s %s k=%zu glibc %zu osv %zu\n", argv[li], f, k, ma, mb); }
                    na = strftime_l(a, k, f, &dates[di], loc); nb = osv___strftime_l(b, k, f, &dates[di], loc);
                    if (na != nb || (na && memcmp(a, b, na + 1))) { ndiff++; printf("SMALL %s %s k=%zu glibc %zu osv %zu\n", argv[li], f, k, na, nb); }
                }
            }
            if (mode == 'g') printf("%s %d %s %zu [%s]\n", argv[li], di, f, na, na ? a : "");
            else if (na != nb || (na && memcmp(a, b, na + 1))) {
                ndiff++;
                if (ndiff < 400) printf("DIFF %s d%d %s glibc %zu [%s] osv %zu [%s]\n", argv[li], di, f, na, na ? a : "", nb, nb ? b : "");
            }
        }
        /* several conversions in one call (state that carries across them) */
        static const char *const multi[] = {
            "%_EY|%EY|%Ey|%-Ey", "%-EY|%_Ey|%EY", "%0EY|%_EY|%Ey", "%_Ey|%EY|%Ey", "%EC|%Ey|%EY|%_EY|%-Ey|%10EC|%^EY",
            "%^a%#b%_d%-H%0e%P%#Z", "%c|%x|%X|%r|%Ec|%Ex|%EX", "%Oy%Od%Om%OH%OM%OS", "%10c%-10x%_10X",
        };
        for (int di = 0; di < ndates; di++)
            for (unsigned k = 0; k < sizeof multi / sizeof *multi; k++) {
                char a[1024], b[1024];
                size_t na = strftime_l(a, sizeof a, multi[k], &dates[di], loc);
                size_t nb = osv___strftime_l(b, sizeof b, multi[k], &dates[di], loc);
                ntot++;
                if (na != nb || (na && memcmp(a, b, na + 1))) { ndiff++; printf("MDIFF %s d%d %s glibc [%s] osv [%s]\n", argv[li], di, multi[k], na ? a : "", nb ? b : ""); }
            }
        /* conversion characters >= 0x80 (not conversions: copied as
         * written), with flags and widths, on both functions */
        static const unsigned cps[] = { 0x80, 0xe9, 0xff, 0x100, 0x3b1, 0x7ff, 0x800, 0x20ac, 0x3042,
                                        0xfffd, 0xffff, 0x10000, 0x1f600, 0x10ffff };
        for (int di = 0; di < 2; di++)
            for (unsigned k = 0; k < sizeof cps / sizeof *cps; k++)
                for (unsigned fi = 0; fi < 6; fi++)
                    for (unsigned wi = 0; wi < 3; wi++) {
                        wchar_t wf[32], wa[256], wb[256];
                        swprintf(wf, 32, L"<%%%s%s%lc>", flags[fi], widths[wi * 3], (wint_t)cps[k]);
                        char f[64];
                        size_t fl = wcstombs(f, wf, sizeof f);
                        size_t ma = wcsftime_l(wa, 256, wf, &dates[di], loc), mb = osv___wcsftime_l(wb, 256, wf, &dates[di], loc);
                        ntot++;
                        if (ma != mb || (ma && wmemcmp(wa, wb, ma + 1))) {
                            ndiff++;
                            printf("WDIFF %s high U+%04X %s glibc %zu osv %zu\n", argv[li], cps[k], fl == (size_t)-1 ? "?" : f, ma, mb);
                        }
                        if (fl == (size_t)-1) continue;
                        char a[256], b[256];
                        size_t na = strftime_l(a, 256, f, &dates[di], loc), nb = osv___strftime_l(b, 256, f, &dates[di], loc);
                        ntot++;
                        if (na != nb || (na && memcmp(a, b, na + 1))) {
                            ndiff++;
                            printf("DIFF %s high U+%04X %s glibc %zu [%s] osv %zu [%s]\n", argv[li], cps[k], f, na, na ? a : "", nb, nb ? b : "");
                        }
                    }
        /* small buffers: the return value on overflow */
        for (int di = 0; di < 3; di++) for (size_t n = 0; n < 40; n++) {
            char a[64], b[64];
            size_t na = strftime_l(a, n, "%c|%A", &dates[di], loc), nb = osv___strftime_l(b, n, "%c|%A", &dates[di], loc);
            ntot++;
            if (na != nb || (na && memcmp(a, b, na + 1))) { ndiff++; printf("DIFF %s small n=%zu glibc %zu osv %zu\n", argv[li], n, na, nb); }
        }
        freelocale(loc);
    }
    printf("SUMMARY %ld of %ld differ\n", ndiff, ntot);
    return ndiff != 0;
}
