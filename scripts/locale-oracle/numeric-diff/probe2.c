/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
/* glibc probe: width, padding and precision with grouping, the I flag, and
 * which category each follows.  probe2 <locale>... */
#define _GNU_SOURCE
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
static void run(void)
{
    char b[256];
    wchar_t w[256];
    static const char *fd[] = { "%'14d|", "%'-14d|", "%'014d", "%'.8d", "%'14.8d|", "%'u", "%'+014d", "%'#x", "%'c",
                                "%I14d|", "%I-14d|", "%I014d", "%'I014d", "%I.8d", "%'I.8d", "%I+d", "%'Ilu" };
    static const char *ff[] = { "%'20f|", "%'-20f|", "%'020f", "%'+020f", "%' 020f", "%'20.2f|", "%'020.0f", "%'#020.0f",
                                "%20f|", "%020f", "%'020g", "%'#.10g", "%'.10g", "%'g", "%'#g", "%'020e", "%'20a|",
                                "%I20f|", "%I020f", "%'I020f", "%I020e", "%I020g", "%I20a|", "%I020a", "%'I.0f", "%'*.*f|" };
    static const int iv[] = { 1234567, -1234567, 1000, 12 };
    static const double dv[] = { 1234567.891, -1234567.891, 12345.5, 0.125 };
    for (unsigned k = 0; k < sizeof fd / sizeof *fd; k++) {
        printf("  %-9s", fd[k]);
        for (unsigned j = 0; j < 4; j++) {
            if (strchr(fd[k], 'l')) snprintf(b, sizeof b, fd[k], (unsigned long)iv[j]);
            else snprintf(b, sizeof b, fd[k], iv[j]);
            printf(" %d", (int)strlen(b)); show(b);
        }
        printf("\n");
    }
    for (unsigned k = 0; k < sizeof ff / sizeof *ff; k++) {
        printf("  %-9s", ff[k]);
        for (unsigned j = 0; j < 4; j++) {
            if (strchr(ff[k], '*')) snprintf(b, sizeof b, ff[k], 20, 3, dv[j]);
            else snprintf(b, sizeof b, ff[k], dv[j]);
            printf(" %d", (int)strlen(b)); show(b);
        }
        printf("\n");
    }
    static const wchar_t *wf[] = { L"%'14d|", L"%'014d", L"%'20f|", L"%'020f", L"%I014d", L"%'I020f", L"%I20e|", L"%'-20f|" };
    for (unsigned k = 0; k < sizeof wf / sizeof *wf; k++) {
        if (wf[k][wcslen(wf[k]) - 1] == L'd') swprintf(w, 256, wf[k], 1234567);
        else swprintf(w, 256, wf[k], 1234567.891);
        printf("  w%-8ls %d", wf[k], (int)wcslen(w)); wshow(w); printf("\n");
    }
}
int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        printf("== %s\n", argv[i]);
        setlocale(LC_ALL, argv[i]); run();
    }
    /* which category: numeric from one, ctype from another */
    printf("== NUMERIC=de_DE CTYPE=fa_IR\n");
    setlocale(LC_ALL, "C"); setlocale(LC_NUMERIC, "de_DE.UTF-8"); setlocale(LC_CTYPE, "fa_IR.UTF-8"); run();
    printf("== NUMERIC=fa_IR CTYPE=C.UTF-8\n");
    setlocale(LC_ALL, "C"); setlocale(LC_NUMERIC, "fa_IR.UTF-8"); setlocale(LC_CTYPE, "C.UTF-8"); run();
    printf("== NUMERIC=fr_FR CTYPE=C\n");
    setlocale(LC_ALL, "C"); setlocale(LC_NUMERIC, "fr_FR.UTF-8"); run();
    printf("== uselocale(de_DE) with global C\n");
    setlocale(LC_ALL, "C"); locale_t l = newlocale(LC_ALL_MASK, "de_DE.UTF-8", 0); uselocale(l); run();
    uselocale(LC_GLOBAL_LOCALE);
}
