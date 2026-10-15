/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

/* Helpers for OSv's floatscan.c and intscan.c: the locale's decimal point
 * and thousands separator in a scan, and scanf's ' flag, as glibc 2.41
 * behaves (measured: scripts/locale-oracle/numeric-diff probes 3, 4, 6, 7):
 *
 * - The rest of a multibyte decimal point or separator is read even past a
 *   field width once its first byte is in it. On a mismatch scanf keeps the
 *   bytes that matched (the number ends there); strtod, and scanf's
 *   integers, give them all back.
 * - With ', separators are accepted anywhere after the first digit of the
 *   integer part (never in the fraction). The whole text is consumed; if it
 *   is not grouped as GROUPING says, the value is that of the longest
 *   validly grouped prefix of the integer part, without fraction or
 *   exponent. Groups count from the right: the rightmost has GROUPING[0]
 *   digits, the next GROUPING[1], ..., the last size repeats, and the
 *   leftmost group has 1 to that many digits, plus (narrow scanf only)
 *   strlen(separator) - 1 more. A part without separators is valid.
 * - Narrow integers with a multibyte separator skip separators without
 *   checking the groups. */

#ifndef OSV_SCAN_H
#define OSV_SCAN_H

#include <limits.h>
#include <string.h>
#include "shgetc.h"
#include "osv-numeric.h"

/* Give back n bytes read with shgetc (also after it returned EOF, which
 * makes shunget a no-op). ponytail: on a real FILE only bytes still in the
 * buffer come back (musl keeps one byte of pushback across a refill); a
 * multibyte item split by a refill on a mismatch is the ceiling. */
static inline void __osv_unget(FILE *f, int n)
{
	f->rpos -= n;
}

/* Match the decimal point dp (if not NULL) or the separator ts (if not
 * NULL) at the byte *c just read. Returns 1 for dp, 2 for ts, 0 for neither
 * (nothing more read), -1 for a partial match. With keep (scanf's floats,
 * glibc 2.41 measured) the matched bytes stay read and *c is the last byte
 * read, for the caller to give back; a field width that ends at the item's
 * first byte is extended to the item's end, one that ends inside it makes
 * a partial match. Without keep (strtod, scanf's integers) a partial match,
 * or a width that ends inside the item, gives back *c and every byte after
 * it, and *c becomes EOF so the caller gives back nothing more. */
static inline int __osv_match(FILE *f, int *c, const char *dp, const char *ts, int keep)
{
	int a = dp && *c == (unsigned char)dp[0];
	int b = ts && *ts && *c == (unsigned char)ts[0];
	off_t r = f->shlim > 0 ? f->shlim - shcnt(f) : -1, cnt;
	int i, d, lifted = 0;
	if (!a && !b) return 0;
	if (keep && r == 0) {
		cnt = shcnt(f);
		__shlim(f, 0);
		f->shcnt += cnt;
		lifted = 1;
	}
	for (i = 1; !(a && !dp[i]) && !(b && !ts[i]); i++) {
		if (r == 0 && !lifted) {
			if (keep) {
				*c = (unsigned char)(a ? dp : ts)[i-1];
			} else {
				__osv_unget(f, i);
				*c = EOF;
			}
			return -1;
		}
		d = shgetc(f);
		if (r > 0) r--;
		a = a && d == (unsigned char)dp[i];
		b = b && d == (unsigned char)ts[i];
		if (!a && !b) {
			if (keep) {
				*c = d;
			} else {
				__osv_unget(f, i + (d >= 0));
				*c = EOF;
			}
			i = -1;
			break;
		}
	}
	if (lifted) {	/* the field ends after what was read beyond the width */
		f->shlim = shcnt(f);
		f->shend = f->rpos;
	}
	return i < 0 ? -1 : a ? 1 : 2;
}

/* The separator ts at the byte *c just read, for scanf's narrow integers,
 * as glibc 2.41 reads it (measured, probe6): the rest of a multibyte
 * separator is read even past a field width. On a mismatch everything
 * after the digits is given back. On a match that overran the width: by
 * at most one byte, the field ends after the separator; by more, the field
 * ends strlen(ts) - 1 bytes before the end of the separator's first byte,
 * which can give back digits already counted (the value keeps them; only
 * %n shows it). Returns 2 for a separator, 0 for none, -1 for the end of
 * the number (*c EOF: nothing more to give back). */
static inline int __osv_match_int(FILE *f, int *c, const char *ts)
{
	off_t len = strlen(ts), r, cnt;
	int i, d, lifted = 0;
	if (*c != (unsigned char)ts[0]) return 0;
	if (len == 1) return 2;
	r = f->shlim > 0 ? f->shlim - shcnt(f) : -1;
	if (r >= 0 && r < len - 1) {
		cnt = shcnt(f);
		__shlim(f, 0);
		f->shcnt += cnt;
		lifted = 1;
	}
	for (i = 1; ts[i]; i++) {
		d = shgetc(f);
		if (d != (unsigned char)ts[i]) {
			__osv_unget(f, i + (d >= 0));
			if (lifted) {
				f->shlim = shcnt(f);
				f->shend = f->rpos;
			}
			*c = EOF;
			return -1;
		}
	}
	if (lifted && r < len - 2) {
		cnt = shcnt(f) - (len - 1) - (len - 1);
		if (cnt < 0) cnt = 0;
		__osv_unget(f, shcnt(f) - cnt);
		f->shlim = shcnt(f);
		f->shend = f->rpos;
		*c = EOF;
		return -1;
	}
	if (lifted) {
		f->shlim = shcnt(f);
		f->shend = f->rpos;
	}
	return 2;
}

/* The grouping check of an integer part, kept as it is read. Groups are
 * numbered from 1 (the leftmost); j = m + 1 is the current one. */
#define OSV_GRP_RING 16
struct osv_grp {
	const char *g;		/* GROUPING */
	int L;			/* its length, at most OSV_GRP_RING */
	long slack;		/* the leftmost group may have this many more digits */
	long g1;		/* size of group 1 (once m > 0) */
	long cur;		/* digits in the current group */
	long m;			/* separators read */
	long firstbad;		/* first group >= 2 whose size is not the repeating one */
	long ring[OSV_GRP_RING];	/* sizes of the last completed groups */
	long n;			/* integer digits read */
	long best;		/* digits of the longest validly grouped prefix */
	long lastcut;		/* the group whose first GROUPING[0] digits ended best */
};

static inline void __osv_grp_init(struct osv_grp *s, const struct osv_num *nm, long slack)
{
	s->g = nm->grp;
	s->L = strlen(nm->grp);
	if (s->L > OSV_GRP_RING) s->L = OSV_GRP_RING; /* ponytail: GROUPING items are 1 to 3 sizes long */
	s->slack = slack;
	s->g1 = s->cur = s->m = s->n = s->best = s->lastcut = 0;
	s->firstbad = LONG_MAX;
}

/* The size of the group k places left of the rightmost (k = 0); LONG_MAX
 * for "no more grouping" (a GROUPING byte of 127 or more). */
static inline long __osv_grp_size(const struct osv_grp *s, long k)
{
	unsigned char c = s->g[k < s->L ? k : s->L - 1];
	return c >= 127 ? LONG_MAX : c;
}

/* Is the prefix ending after the first size(0) digits of group j valid? */
static inline int __osv_grp_cut(const struct osv_grp *s, long j)
{
	long i, lo = j - s->L + 2, e;
	if (lo < 2) lo = 2;
	if (s->firstbad <= j - s->L + 1) return 0;
	for (i = lo; i < j; i++)
		if (s->ring[i % OSV_GRP_RING] != __osv_grp_size(s, j - i)) return 0;
	e = __osv_grp_size(s, j - 1);
	return e == LONG_MAX || s->g1 <= e + s->slack;
}

/* A digit of the integer part; 1 if best now ends here. */
static inline int __osv_grp_digit(struct osv_grp *s)
{
	s->n++;
	s->cur++;
	if (s->m && s->cur == __osv_grp_size(s, 0) && __osv_grp_cut(s, s->m + 1)) {
		s->best = s->n;
		s->lastcut = s->m + 1;
		return 1;
	}
	return 0;
}

/* A separator; 1 if best now ends here (group 1 alone is always valid). */
static inline int __osv_grp_gotsep(struct osv_grp *s)
{
	long j = s->m + 1;
	s->m++;
	if (j == 1) {
		s->g1 = s->cur;
		s->best = s->n;
		s->cur = 0;
		return 1;
	}
	s->ring[j % OSV_GRP_RING] = s->cur;
	if (s->firstbad == LONG_MAX && s->cur != __osv_grp_size(s, LONG_MAX)) s->firstbad = j;
	s->cur = 0;
	return 0;
}

/* Is the whole integer part validly grouped? */
static inline int __osv_grp_full(const struct osv_grp *s)
{
	return !s->m || (s->lastcut == s->m + 1 && s->cur == __osv_grp_size(s, 0));
}

#endif
