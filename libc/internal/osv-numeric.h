/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

/* The LC_NUMERIC items printf, scanf and strtod use, as glibc 2.41 applies
 * them (measured: scripts/locale-oracle/numeric-diff). With conf_locale=0
 * every locale is C here, so the C behaviour cannot change. */

#ifndef OSV_NUMERIC_H
#define OSV_NUMERIC_H

#include <locale.h>
#include <wchar.h>

struct osv_num {
	const char *dp;		/* DECIMAL_POINT (may be multibyte) */
	const char *ts;		/* THOUSEP, "" for none */
	const char *grp;	/* GROUPING: group sizes from the right */
	wchar_t dpw, tsw;	/* the same as wide characters (tsw 0: none) */
	const char *dig[10];	/* glibc's I flag: LC_CTYPE OUTDIGITn, or dig[0] 0: ASCII */
};

#if OSV_CONF_LOCALE
/* Fill n from l's LC_NUMERIC (NULL or LC_GLOBAL_LOCALE: the thread's
 * current locale), and the digits from its LC_CTYPE if digits is set. */
__attribute__((visibility("hidden"))) void __osv_num(struct osv_num *n, locale_t l, int digits);
#else
static inline void __osv_num(struct osv_num *n, locale_t l, int digits)
{
	(void)l; (void)digits;
	n->dp = "."; n->ts = ""; n->grp = "";
	n->dpw = '.'; n->tsw = 0;
	n->dig[0] = 0;
}
#endif

/* strtod, strtof, strtold (prec 1, 0, 2) in locale l (NULL: the current
 * one); libc/stdlib/strtod.c */
__attribute__((visibility("hidden"))) long double __osv_strtox(const char *, char **, int prec, locale_t l);

/* vfwprintf formats through vfprintf into a FILE with this flag: widths
 * of integers then count characters, not bytes (floats always count
 * characters, as glibc's do). */
#define OSV_F_WCHARS 0x10000

/* Decode one UTF-8 character at *s and step past it (OSv loads only UTF-8
 * locales; C's items are ASCII). A malformed byte is returned as itself. */
static inline wchar_t __osv_u8dec(const char **s)
{
	const unsigned char *p = (const unsigned char *)*s;
	int n, i;
	wchar_t wc;
	if (*p < 0xc0) n = 0, wc = *p;
	else if (*p >= 0xf0) n = 3, wc = *p & 7;
	else if (*p >= 0xe0) n = 2, wc = *p & 15;
	else n = 1, wc = *p & 31;
	for (i = 1; i <= n; i++) {
		if ((p[i] & 0xc0) != 0x80) {
			*s += 1;
			return *p;
		}
		wc = wc << 6 | (p[i] & 63);
	}
	*s += 1 + n;
	return wc;
}

/* The number of characters of a UTF-8 string. */
static inline long __osv_u8chars(const char *s)
{
	long n = 0;
	for (; *s; s++) n += ((unsigned char)*s & 0xc0) != 0x80;
	return n;
}

/* Whether ' groups at all: a separator and a first group size (glibc
 * stores "no further grouping" as 127 or 255). */
static inline int __osv_grouping(const struct osv_num *n)
{
	unsigned char g = *n->grp;
	return *n->ts && g && g < 127;
}

/* Is there a separator after the digit that has k > 0 digits to its right?
 * Group sizes run from the right; the last one repeats, a byte >= 127 stops
 * the grouping. */
static inline int __osv_grp_sep(const char *g, long k)
{
	long pos = 0, n = 0;
	for (; *g; g++) {
		if ((unsigned char)*g >= 127) return 0;
		n = (unsigned char)*g;
		pos += n;
		if (pos == k) return 1;
		if (pos > k) return 0;
	}
	return n > 0 && (k - pos) % n == 0;
}

/* The number of separators in an integer part of nd digits. */
static inline long __osv_grp_count(const char *g, long nd)
{
	long k, c = 0;
	for (k = 1; k < nd; k++) c += __osv_grp_sep(g, k);
	return c;
}

#endif
