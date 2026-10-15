/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

/* The words and locales of tests/tst-locale-collate.cc, shared with its
 * expected-output generator (scripts/locale-oracle/collate/gen-collate-expected.c).
 * Accents, case, punctuation, digits, spaces, hyphens, ligatures, CJK,
 * Cyrillic, Greek, Arabic, Thai, Hangul, contractions (cs_CZ "ch"), backward
 * accents (fr_CA), combining marks, and invalid UTF-8. */

#ifndef TST_LOCALE_COLLATE_WORDS_H
#define TST_LOCALE_COLLATE_WORDS_H

#include <stddef.h>
#include <stdint.h>

static const char *const collate_locales[] = {
    "C", "C.UTF-8", "en_US.UTF-8", "en_GB.UTF-8", "de_DE.UTF-8", "fr_FR.UTF-8", "es_ES.UTF-8",
    "it_IT.UTF-8", "pt_BR.UTF-8", "nl_NL.UTF-8", "sv_SE.UTF-8", "pl_PL.UTF-8", "ru_RU.UTF-8",
    "ja_JP.UTF-8", "zh_CN.UTF-8", "ko_KR.UTF-8", "tr_TR.UTF-8", "cs_CZ.UTF-8", "fr_CA.UTF-8",
};

static const char *const collate_words[] = {
    "", " ", "-", "'", "a", "A", "b", "B", "z", "Z", "aa", "Aa", "ab", "aB", "a b", "a-b", "a'b",
    "ab-", "-ab", "a  b", "co-op", "coop", "co op", "Co-op", "cote", "cot\xc3\xa9", "c\xc3\xb4te",
    "c\xc3\xb4t\xc3\xa9", "e", "E", "\xc3\xa9", "\xc3\x89", "\xc3\xa8", "\xc3\xaa", "e\xcc\x81",
    "\xc3\xa9t\xc3\xa9", "ete", "resume", "r\xc3\xa9sum\xc3\xa9", "R\xc3\xa9sum\xc3\xa9", "\xc3\xa6",
    "ae", "AE", "\xc5\x93", "oe", "\xc3\x9f", "ss", "SS", "\xef\xac\x81", "fi", "\xc3\xa5", "\xc3\xa4",
    "\xc3\xb6", "o", "\xc3\xb8", "\xc3\xbc", "u", "\xc3\xb1", "n", "\xc5\x82", "l", "\xc4\xb1", "i",
    "I", "\xc4\xb0", "c", "C", "ch", "Ch", "CH", "h", "H", "cz", "hz", "\xc4\x8d", "\xc5\x99", "r",
    "\xc5\xa1", "s", "\xc5\xbe", "0", "1", "2", "9", "10", "01", "1.5", "1,5", "-1", "$1", "1a", "a1",
    "\xc2\xbd", "\xe4\xb8\x80", "\xe4\xba\x8c", "\xe4\xb8\xad\xe6\x96\x87", "\xe6\x97\xa5\xe6\x9c\xac",
    "\xe3\x81\x82", "\xe3\x82\xa2", "\xea\xb0\x80", "\xea\xb0\x81", "\xd0\xb0", "\xd0\x90",
    "\xd0\xb1", "\xd1\x91", "\xd0\xb5", "\xce\xb1", "\xce\x91", "\xd8\xa7", "\xd8\xa8",
    "\xe0\xb8\x81", "\xe0\xb9\x80\xe0\xb8\x81", "\xf0\x9f\x98\x80", "\xc2\xa0", "\xc2\xb7",
    "l\xc2\xb7l", "ll", "\x01", "\x7f", "\x80", "\xff", "\xc2", "a\x80", "\xcc\x81", "\xcc\x80 A",
    "x\xcc\x81\xcc\x80y", "\xc3\xa9\xc3\xa8", "\xc3\xa8\xc3\xa9", "PostgreSQL", "postgres",
    "Postgres", "M\xc3\xbcller", "Mueller", "Muller", "O'Brien", "OBrien",
};

/* One word of a sorted list: its index in collate_words, the sign of
 * strcoll and wcscoll against the next word (wcscoll 2: not valid UTF-8),
 * and its strxfrm and wcsxfrm output as a length and an FNV-1a hash (of the
 * bytes; of the wide units as 4 little-endian bytes each); wn -1: not valid
 * UTF-8. */
struct collate_expect {
    int word, coll, wcoll;
    size_t n;
    uint64_t xh;
    long wn;
    uint64_t wh;
};

static uint64_t collate_fnv(const unsigned char *p, size_t n, uint64_t h)
{
    for (size_t i = 0; i < n; i++) {
        h = (h ^ p[i]) * 0x100000001b3ull;
    }
    return h;
}
#define COLLATE_FNV0 0xcbf29ce484222325ull

#endif
