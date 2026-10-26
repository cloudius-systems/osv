/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
/* Measure what glibc's nl_langinfo_l() returns for every item of every
 * category of a locale (run against glibc; no glibc source was read):
 *   item-dump <locale>...    (LOCPATH as for the program under test)
 * For each locale, category c (0..12, not 6) and index i (0..nmax-1) one line
 *   <locale> <c> <i> W <value>                    not inside any mapping
 *   <locale> <c> <i> P <map> <off> <value> <hex bytes>
 *                                                 inside a mapping: its path (or
 *                                                 "-"), the offset from the
 *                                                 mapping's start, the value (hex),
 *                                                 and up to 512 bytes from there,
 *                                                 within the mapping
 * (a word item's value can fall inside an unrelated mapping: glibc returns
 * the word in the low 32 bits; item-types.py checks the offset)
 * and per locale and category "<locale> <c> E <addr>": the address of the
 * value of an index past every category's items (glibc's "" for unknown
 * items), so the classifier can tell where the items end. item-types.py
 * classifies the lines. */
#define _GNU_SOURCE
#include <langinfo.h>
#include <locale.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct map {
    uintptr_t lo, hi;
    char path[512];
};
static struct map maps[65536];
static int nmaps;

static void read_maps(void)
{
    FILE *f = fopen("/proc/self/maps", "r");
    char line[1024];
    nmaps = 0;
    while (f && fgets(line, sizeof line, f)) {
        if (nmaps == 65536) {
            fprintf(stderr, "item-dump: more than 65536 mappings\n");
            exit(2);
        }
        struct map *m = &maps[nmaps];
        unsigned long lo, hi;
        int n = 0;
        if (sscanf(line, "%lx-%lx %*s %*s %*s %*s %n", &lo, &hi, &n) < 2) continue;
        m->lo = lo;
        m->hi = hi;
        snprintf(m->path, sizeof m->path, "%s", line[n] && line[n] != '\n' ? line + n : "-\n");
        m->path[strcspn(m->path, "\n")] = 0;
        nmaps++;
    }
    if (f) fclose(f);
}

static const struct map *find(uintptr_t p)
{
    for (int i = 0; i < nmaps; i++)
        if (p >= maps[i].lo && p < maps[i].hi) return &maps[i];
    return 0;
}

int main(int argc, char **argv)
{
    int nmax = 400;
    for (int a = 1; a < argc; a++) {
        locale_t l = newlocale(LC_ALL_MASK, argv[a], 0);
        if (!l) {
            printf("%s NOLOCALE\n", argv[a]);
            continue;
        }
        read_maps();
        for (int c = 0; c <= 12; c++) {
            if (c == 6) continue;
            uintptr_t past = (uintptr_t)nl_langinfo_l((c << 16) | 0xfff0, l);
            printf("%s %d E %#lx\n", argv[a], c, (unsigned long)past);
            for (int i = 0; i < nmax; i++) {
                uintptr_t v = (uintptr_t)nl_langinfo_l((c << 16) | i, l);
                const struct map *m = find(v);
                if (!m) {
                    printf("%s %d %d W %lu\n", argv[a], c, i, (unsigned long)v);
                    continue;
                }
                printf("%s %d %d P %s %lu %#lx ", argv[a], c, i, m->path, (unsigned long)(v - m->lo),
                       (unsigned long)v);
                for (uintptr_t p = v; p < m->hi && p < v + 512; p++) printf("%02x", *(unsigned char *)p);
                printf("%s\n", v == past ? " PAST" : "");
            }
        }
        freelocale(l);
    }
    return 0;
}
