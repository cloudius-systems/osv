/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
// Host differential of the directory lookup: for each name, which locale
// glibc's newlocale() finds (its LC_IDENTIFICATION language, or NULL) and
// which OSv's osv_locale_load(LC_IDENTIFICATION) finds, with the same
// LOCPATH. Build (from the OSv tree):
//   g++ -std=gnu++17 -O1 -fsanitize=address,undefined -Ilibc/locale \
//       scripts/locale-oracle/time-diff/fbdiff.cc libc/locale/locale-data.cc -o fbd
//   LOCPATH=<dirs> ./fbd <name>...      exit 1 if any differ
#include "locale-data.h"

#include <cstdio>
#include <cstring>
#include <langinfo.h>
#include <locale.h>

int main(int argc, char **argv)
{
    int diff = 0;
    for (int i = 1; i < argc; i++) {
        locale_t l = newlocale(LC_IDENTIFICATION_MASK, argv[i], nullptr);
        const char *g = l ? nl_langinfo_l(_NL_IDENTIFICATION_LANGUAGE, l) : "NULL";
        const osv_locale_data *d = osv_locale_load(LC_IDENTIFICATION, argv[i]);
        const char *o = d ? osv_locale_item(d, _NL_IDENTIFICATION_LANGUAGE & 0xffff) : "NULL";
        bool same = !strcmp(g, o);
        diff |= !same;
        printf("%s %-24s glibc %-20s osv %s\n", same ? "  " : "!!", argv[i], g, o);
        if (l) {
            freelocale(l);
        }
    }
    return diff;
}
