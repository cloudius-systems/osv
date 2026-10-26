/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
/* glibc probe: LC_NUMERIC facts and printf with the locale's numeric items.
 *   probe1 <locale>...   (LOCPATH points at the test data) */
#define _GNU_SOURCE
#include <langinfo.h>
#include <limits.h>
#include <locale.h>
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
static void wshow(const wchar_t *s)
{
    putchar('[');
    for (; *s; s++) if (*s < 32 || *s > 126) printf("\\u%04x", (unsigned)*s); else putchar(*s);
    putchar(']');
}
int main(int argc, char **argv)
{
    char b[512];
    wchar_t w[512];
    for (int i = 1; i < argc; i++) {
        if (!setlocale(LC_ALL, argv[i])) { printf("%s: setlocale failed\n", argv[i]); continue; }
        struct lconv *lc = localeconv();
        printf("== %s dp=", argv[i]); show(lc->decimal_point); printf(" ts="); show(lc->thousands_sep);
        printf(" grouping=");
        for (const char *g = lc->grouping; *g; g++) printf("%d,", *g);
        printf(" RADIX="); show(nl_langinfo(RADIXCHAR)); printf(" THOUSEP="); show(nl_langinfo(THOUSEP));
        printf("\n");
        static const char *ff[] = { "%f", "%.0f", "%#.0f", "%e", "%#.0e", "%g", "%#g", "%a", "%A", "%.0a", "%#.0a",
                                    "%'f", "%'.0f", "%'e", "%'g", "%'a", "%'.3g", "%'.10g", "%'020f", "%'-20f|", "%'+f",
                                    "%If", "%Ie", "%Ig", "%'If", "%'.1F", "%F", "%'F", "%'G", "%'E", "%Lf", "%'Lf" };
        static const double vv[] = { 1234567.891, -0.5, 1e300, 0.0, 12345.0, 1e-300 };
        for (unsigned k = 0; k < sizeof ff / sizeof *ff; k++) {
            printf("  %-8s", ff[k]);
            for (unsigned j = 0; j < sizeof vv / sizeof *vv; j++) {
                if (ff[k][strlen(ff[k]) - 1 - (ff[k][strlen(ff[k]) - 1] == '|')] == 'f' && strchr(ff[k], 'L'))
                    snprintf(b, sizeof b, ff[k], (long double)vv[j]);
                else
                    snprintf(b, sizeof b, ff[k], vv[j]);
                if (vv[j] == 1e300 && strchr(ff[k], 'f') && !strchr(ff[k], 'a')) { b[40] = 0; }
                printf(" "); show(b);
            }
            printf("\n");
        }
        static const char *fi[] = { "%d", "%'d", "%'i", "%'u", "%'x", "%'o", "%'X", "%'ld", "%'lld", "%'10d|",
                                    "%'-12d|", "%'012d", "%'+d", "%' d", "%'.10d", "%Id", "%'Id", "%Iu", "%Ix", "%'hhd", "%'zd" };
        static const long long iv[] = { 0, 1234567, -1234567, INT_MIN, 999, 1000 };
        for (unsigned k = 0; k < sizeof fi / sizeof *fi; k++) {
            printf("  %-8s", fi[k]);
            for (unsigned j = 0; j < sizeof iv / sizeof *iv; j++) {
                if (strstr(fi[k], "ll")) snprintf(b, sizeof b, fi[k], iv[j]);
                else if (strchr(fi[k], 'l') || strchr(fi[k], 'z')) snprintf(b, sizeof b, fi[k], (long)iv[j]);
                else snprintf(b, sizeof b, fi[k], (int)iv[j]);
                printf(" "); show(b);
            }
            printf("\n");
        }
        snprintf(b, sizeof b, "%'lld", LLONG_MAX); printf("  LLONG_MAX "); show(b); printf("\n");
        snprintf(b, sizeof b, "%'f %'f %'f %'f", 1.0/0.0, -1.0/0.0, 0.0/0.0, -0.0); printf("  infnan "); show(b); printf("\n");
        static const wchar_t *wf[] = { L"%f", L"%'f", L"%'d", L"%e", L"%a", L"%g", L"%'.0f", L"%#.0f", L"%If", L"%'Id" };
        for (unsigned k = 0; k < sizeof wf / sizeof *wf; k++) {
            if (wf[k][wcslen(wf[k]) - 1] == L'd') swprintf(w, 512, wf[k], 1234567);
            else swprintf(w, 512, wf[k], 1234567.891);
            printf("  w%-7ls ", wf[k]); wshow(w); printf("\n");
        }
    }
}
