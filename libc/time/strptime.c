/*
 * This file is an OSv copy of musl's src/time/strptime.c, modified for OSv by
 * Greg Burd in 2026 (rewritten to glibc 2.41's measured parsing rules, with
 * eras and alternative digits from the locale).
 *
 * musl as a whole is licensed under the following standard MIT license:
 *
 * Copyright (c) 2005-2020 Rich Felker, et al.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/* strptime with glibc 2.41's observable behaviour (measured by running it;
 * no glibc source was read; scripts/locale-oracle/time-diff/spdiff.c):
 * - white space in the format matches any amount of white space; any other
 *   character matches itself exactly;
 * - numbers skip leading white space, take at most the conversion's digits
 *   and stop early when another digit would exceed the maximum; out of range
 *   fails. Flags (_ - 0 ^ #) and a field width are accepted and ignored;
 * - names match case-insensitively, the longest of the locale's full and
 *   abbreviated names (months also the alternative forms) and the C
 *   locale's; a C abbreviated weekday is recognised but, as in glibc, not
 *   consumed;
 * - %p sets AM/PM, applied at the end only to an hour from %I or %l; %H and
 *   %k after %I cancel it. %y alone is 1969..2068; %C combines with %y (or
 *   alone gives the century's first year) wherever it appears; %r falls
 *   back to %I:%M:%S %p (C AM/PM) when T_FMT_AMPM is empty or fails;
 * - %E: %Ec %Ex %EX use the era formats when the locale has them, %EC the
 *   era name, %Ey the year in that era (era 0 without %EC), %EY the full
 *   era format; without eras, or when no era matches, %EC is %C and %EY is
 *   %Y. %O: the locale's ALT_DIGITS (the longest match), else ASCII
 *   digits, but only once per call: a later %O number that is not
 *   ALT_DIGITS fails;
 * - at the end, with a date part given: tm_wday and tm_yday from the year,
 *   month and day (or month and day from %j), and from %U/%W with a weekday;
 * - fields are stored as they are parsed, so a failed parse leaves them;
 *   a failed sub-format (%c %D %F %T %x ...) leaves tm as it was before it;
 * - the locale's formats are data and nest (an era format may hold %EY,
 *   which tries every era), so parsing is bounded: 8 levels deep, and
 *   OSV_TIME_WORK steps (a format character or conversion, an era tried) for
 *   each conversion of the caller's format; past that the call fails. */

#include <stdlib.h>
#include <langinfo.h>
#include <locale.h>
#include <time.h>
#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <limits.h>
#include "locale_impl.h"
#include "osv-time.h"

#define TIME_ITEM(i) ((LC_TIME << 16) | (i))
#define ALTMON_ITEM(m) TIME_ITEM(111 + (m))
#define ABALTMON_ITEM(m) TIME_ITEM(135 + (m))
#define ERA_NUM_ITEM TIME_ITEM(50)

enum { UNDECIDED, LOCALE, CNAMES };

struct st {
	locale_t loc;
	/* Which names and formats are in use: glibc starts undecided; a
	 * locale string that differs from the C locale's and matches decides
	 * LOCALE; a C string used because the locale's failed decides CNAMES.
	 * Once decided, the other side is no longer tried. */
	int decided;
	int have_I, is_pm, have_wday, have_yday, have_mon, have_mday;
	int have_uweek, have_wweek, week_no, want_xday;
	int have_century, century, want_century;
	int ec;		/* era: -1 none, -2 %EC fell back to %C, else index */
	int have_ey, ey;
	int in_ey;	/* within %EY: the era whose format is being matched, else -1 */
	int alt_ascii;	/* a %O number fell back to ASCII digits: glibc then
			 * fails every later %O number of the call */
	int depth;
	long *work;	/* steps left in this conversion, < 0: failed (not
			 * restored with a failed sub-format's state) */
};

/* One step; 1 once the budget is spent. */
static int spend(struct st *st)
{
	if (*st->work >= 0) --*st->work;
	return *st->work < 0;
}

static const char *li(struct st *st, nl_item it)
{
	return __nl_langinfo_l(it, st->loc);
}

static const char *cli(nl_item it)
{
	return __nl_langinfo_l(it, C_LOCALE);
}

static unsigned nera(struct st *st)
{
	return (unsigned)(uintptr_t)__nl_langinfo_l(ERA_NUM_ITEM, st->loc);
}

static int era_at(struct st *st, unsigned i, struct __osv_era *e)
{
	const char *s = __osv_nl_list(ERA, i, st->loc);
	return s && __osv_era_parse(s, e);
}

/* The Gregorian year of year ey of era e, counted in the era's direction
 * from its start year, as glibc's strptime does. */
static long long era_greg(const struct __osv_era *e, long long ey)
{
	long long sy = e->start / 10000 - (e->start % 10000 < 0);
	return e->dir == '-' ? sy - (ey - e->offset) : sy + (ey - e->offset);
}

/* Is year ey of era e within the era's years? */
static int era_valid(const struct __osv_era *e, long long ey)
{
	long long y = era_greg(e, ey);
	long long a = e->start == LLONG_MIN ? LLONG_MIN : e->start == LLONG_MAX ? LLONG_MAX : e->start / 10000 - (e->start % 10000 < 0);
	long long b = e->end == LLONG_MIN ? LLONG_MIN : e->end == LLONG_MAX ? LLONG_MAX : e->end / 10000 - (e->end % 10000 < 0);
	return a <= b ? y >= a && y <= b : y >= b && y <= a;
}

static const char *skipsp(const char *s)
{
	while (isspace((unsigned char)*s)) s++;
	return s;
}

/* glibc's number rule: at most n digits, stop when another would exceed hi. */
static const char *num(const char *s, int lo, int hi, int n, int *v)
{
	s = skipsp(s);
	if (*s - '0' >= 10U) return 0;
	int x = *s++ - '0';
	while (--n > 0 && x * 10 <= hi && *s - '0' < 10U) x = x * 10 + (*s++ - '0');
	if (x < lo || x > hi) return 0;
	*v = x;
	return s;
}

/* %O number: the longest matching ALT_DIGITS string, else ASCII digits. */
static const char *altnum(struct st *st, const char *s, int lo, int hi, int n, int *v)
{
	const char *t = skipsp(s);
	size_t best = 0;
	int bv = -1;
	for (unsigned i = 0; i < 100; i++) {
		const char *a = __osv_nl_list(ALT_DIGITS, i, st->loc);
		if (!a) break;
		size_t k = strlen(a);
		if (k > best && !strncmp(t, a, k)) best = k, bv = i;
	}
	if (bv >= 0) {
		if (bv < lo || bv > hi) return 0;
		*v = bv;
		return t + best;
	}
	if (st->alt_ascii) return 0;
	st->alt_ascii = 1;
	return num(s, lo, hi, n, v);
}

static const char *name(const char *s, const char *n, size_t *len)
{
	*len = strlen(n);
	return strncasecmp(s, n, *len) ? 0 : s;
}

/* The longest case-insensitive match of s among n candidates; a candidate
 * flagged nocons matches but consumes nothing. -1 if none. */
struct cand {
	const char *str;
	int val, nocons, side;	/* side: LOCALE (and differs from C), CNAMES, or UNDECIDED */
};

static int longest(struct st *st, const char *s, const struct cand *c, int n, size_t *len)
{
	int best = -1;
	size_t bl = 0, k;
	for (int i = 0; i < n; i++) {
		if (!*c[i].str || !name(s, c[i].str, &k)) continue;
		if (c[i].nocons) k = 0;
		if (best < 0 || k > bl) best = i, bl = k;
	}
	*len = bl;
	if (best < 0) return -1;
	if (c[best].side != UNDECIDED) st->decided = c[best].side;
	return c[best].val;
}

/* Add the locale's string it (and the C locale's it, unless the locale is
 * C) as candidates for value v, as the decided state allows. */
static int add(struct st *st, struct cand *cd, int n, nl_item it, int v, int c_nocons)
{
	const char *l = li(st, it), *c = cli(it);
	int same = !strcmp(l, c);
	if (st->decided != CNAMES) cd[n++] = (struct cand){ l, v, 0, same ? UNDECIDED : LOCALE };
	if (st->decided != LOCALE && st->loc != C_LOCALE && !same)
		cd[n++] = (struct cand){ c, v, c_nocons, CNAMES };
	return n;
}

static const char *sub(struct st *st, const char *s, const char *f, struct tm *tm);

/* A locale format item (D_T_FMT ...): the locale's format, then, unless
 * decided for the locale, the C locale's (glibc's fallback). lf overrides
 * the locale's format (the era formats), "" for none. */
static const char *fmt_item(struct st *st, const char *s, nl_item it, const char *lf, struct tm *tm)
{
	const char *l = lf && *lf ? lf : li(st, it), *c = cli(it), *r = 0;
	if (st->decided != CNAMES && *l) {
		r = sub(st, s, l, tm);
		if (r && strcmp(l, c)) st->decided = LOCALE;
	}
	if (!r && st->decided != LOCALE) {
		int d = st->decided;
		if (st->loc != C_LOCALE) st->decided = CNAMES;
		r = sub(st, s, c, tm);
		if (!r) st->decided = d;
	}
	return r;
}

static const char *parse(struct st *st, const char *s, const char *f, struct tm *tm);

static const char *sub(struct st *st, const char *s, const char *f, struct tm *tm)
{
	struct tm save = *tm;
	struct st ssave = *st;
	if (st->depth > 8) return 0;
	st->depth++;
	s = parse(st, s, f, tm);
	st->depth--;
	if (!s) *tm = save, *st = ssave;	/* glibc: a failed sub-format stores nothing */
	return s;
}

static const char *parse(struct st *st, const char *s, const char *f, struct tm *tm)
{
	int v, i;
	size_t len;
	struct __osv_era era;
	while (*f) {
		if (!st->depth && *st->work >= 0) *st->work = OSV_TIME_WORK;
		if (spend(st)) return 0;
		if (isspace((unsigned char)*f)) {
			s = skipsp(s);
			f++;
			continue;
		}
		if (*f != '%') {
			if (*s++ != *f++) return 0;
			continue;
		}
		f++;
		while (*f == '_' || *f == '-' || *f == '0' || *f == '^' || *f == '#') f++;
		while (*f - '0' < 10U) f++;
		int mod = 0;
		if (*f == 'E' || *f == 'O') mod = *f++;
		int c = *f++;
		if (mod == 'E' && !strchr("cCxXyY", c)) return 0;
		if (mod == 'O' && !strchr("bBdehHImMSUVwWy", c)) return 0;
		switch (c) {
		case 'a': case 'A': {
			/* The locale's names, then the C locale's: glibc recognises
			 * a C abbreviated weekday but does not consume it. */
			struct cand cd[7 * 2 * 2];
			int n = 0;
			for (int k = 0; k < 7; k++) {
				n = add(st, cd, n, DAY_1 + k, k, 0);
				n = add(st, cd, n, ABDAY_1 + k, k, 1);
			}
			if ((i = longest(st, s, cd, n, &len)) < 0) return 0;
			s += len;
			tm->tm_wday = i;
			st->have_wday = 1;
			break;
		}
		case 'b': case 'B': case 'h': {
			struct cand cd[12 * 4 * 2];
			int n = 0;
			for (int k = 0; k < 12; k++) {
				n = add(st, cd, n, MON_1 + k, k, 0);
				n = add(st, cd, n, ABMON_1 + k, k, 0);
				n = add(st, cd, n, ALTMON_ITEM(k), k, 0);
				n = add(st, cd, n, ABALTMON_ITEM(k), k, 0);
			}
			if ((i = longest(st, s, cd, n, &len)) < 0) return 0;
			s += len;
			tm->tm_mon = i;
			st->have_mon = 1;
			st->want_xday = 1;
			break;
		}
		case 'c':
			if (!(s = fmt_item(st, s, D_T_FMT, mod == 'E' && nera(st) ? li(st, ERA_D_T_FMT) : 0, tm))) return 0;
			st->want_xday = 1;
			break;
		case 'C':
			if (mod == 'E' && nera(st)) {
				/* The first era with this name, or with %Ey already
				 * read, the first one that year is valid in. */
				s = skipsp(s);
				unsigned n = nera(st);
				for (unsigned k = 0; k < n && k < 1000; k++) {
					if (spend(st)) return 0;
					if (st->in_ey >= 0 && (unsigned)st->in_ey != k) continue;
					if (!era_at(st, k, &era) || !era.name_len) continue;
					if (strncmp(s, era.name, era.name_len)) continue;
					if (st->have_ey && !era_valid(&era, st->ey)) continue;
					st->ec = k;
					s += era.name_len;
					goto era_found;
				}
				if (st->in_ey >= 0) return 0;
				goto plain_C;	/* no era name: glibc reads %C */
			era_found:
				st->decided = LOCALE;
				break;
			}
		plain_C:
			if (!(s = num(s, 0, 99, 2, &v))) return 0;
			st->century = v;
			st->have_century = 1;
			st->want_xday = 1;
			if (mod == 'E') st->ec = -2;
			break;
		case 'd': case 'e':
			if (!(s = mod == 'O' ? altnum(st, s, 1, 31, 2, &v) : num(s, 1, 31, 2, &v))) return 0;
			tm->tm_mday = v;
			st->have_mday = 1;
			st->want_xday = 1;
			break;
		case 'D':
			if (!(s = sub(st, s, "%m/%d/%y", tm))) return 0;
			st->want_xday = 1;
			break;
		case 'F':
			if (!(s = sub(st, s, "%Y-%m-%d", tm))) return 0;
			st->want_xday = 1;
			break;
		case 'g':
			if (!(s = num(s, 0, 99, 2, &v))) return 0;
			break;
		case 'G':
			s = skipsp(s);
			if (*s - '0' >= 10U) return 0;
			while (*s - '0' < 10U) s++;
			break;
		case 'k':
		case 'H':
			if (!(s = mod == 'O' ? altnum(st, s, 0, 23, 2, &v) : num(s, 0, 23, 2, &v))) return 0;
			tm->tm_hour = v;
			st->have_I = 0;
			break;
		case 'l':
		case 'I':
			if (!(s = mod == 'O' ? altnum(st, s, 1, 12, 2, &v) : num(s, 1, 12, 2, &v))) return 0;
			tm->tm_hour = v % 12;
			st->have_I = 1;
			break;
		case 'j':
			if (!(s = num(s, 1, 366, 3, &v))) return 0;
			tm->tm_yday = v - 1;
			st->have_yday = 1;
			break;
		case 'm':
			if (!(s = mod == 'O' ? altnum(st, s, 1, 12, 2, &v) : num(s, 1, 12, 2, &v))) return 0;
			tm->tm_mon = v - 1;
			st->have_mon = 1;
			st->want_xday = 1;
			break;
		case 'M':
			if (!(s = mod == 'O' ? altnum(st, s, 0, 59, 2, &v) : num(s, 0, 59, 2, &v))) return 0;
			tm->tm_min = v;
			break;
		case 'n': case 't':
			s = skipsp(s);
			break;
		case 'p':
			/* The locale's AM, then PM (the first that matches, even
			 * an empty one), then the C locale's. */
			for (i = 0; i < 4; i++) {
				int c = i >= 2;
				if (c ? st->decided == LOCALE || st->loc == C_LOCALE : st->decided == CNAMES) continue;
				const char *l = c ? cli(i & 1 ? PM_STR : AM_STR) : li(st, i & 1 ? PM_STR : AM_STR);
				if (!name(s, l, &len)) continue;
				if (c) st->decided = CNAMES;
				else if (strcmp(l, cli(i & 1 ? PM_STR : AM_STR))) st->decided = LOCALE;
				break;
			}
			if (i == 4) return 0;
			st->is_pm = i & 1;
			s += len;
			break;
		case 'r':
			/* T_FMT_AMPM, else (empty or not matching, and not
			 * decided for the locale) the C locale's %I:%M:%S %p. */
			if (!(s = fmt_item(st, s, T_FMT_AMPM, 0, tm))) return 0;
			break;
		case 'R':
			if (!(s = sub(st, s, "%H:%M", tm))) return 0;
			break;
		case 's': {
			if (*s - '0' >= 10U) return 0;
			long long t = 0;
			for (; *s - '0' < 10U; s++)
				if (t < LLONG_MAX / 10) t = t * 10 + (*s - '0');
			time_t tt = t;
			if (tt != t || !localtime_r(&tt, tm)) return 0;
			break;
		}
		case 'S':
			if (!(s = mod == 'O' ? altnum(st, s, 0, 61, 2, &v) : num(s, 0, 61, 2, &v))) return 0;
			tm->tm_sec = v;
			break;
		case 'T':
			if (!(s = sub(st, s, "%H:%M:%S", tm))) return 0;
			break;
		case 'u':
			if (!(s = num(s, 1, 7, 1, &v))) return 0;
			tm->tm_wday = v % 7;
			st->have_wday = 1;
			break;
		case 'U':
		case 'W':
			if (!(s = mod == 'O' ? altnum(st, s, 0, 53, 2, &v) : num(s, 0, 53, 2, &v))) return 0;
			st->week_no = v;
			if (c == 'U') st->have_uweek = 1, st->have_wweek = 0;
			else st->have_wweek = 1, st->have_uweek = 0;
			break;
		case 'V':
			if (!(s = mod == 'O' ? altnum(st, s, 0, 53, 2, &v) : num(s, 0, 53, 2, &v))) return 0;
			break;
		case 'w':
			if (!(s = mod == 'O' ? altnum(st, s, 0, 6, 1, &v) : num(s, 0, 6, 1, &v))) return 0;
			tm->tm_wday = v;
			st->have_wday = 1;
			break;
		case 'x':
			if (!(s = fmt_item(st, s, D_FMT, mod == 'E' && nera(st) ? li(st, ERA_D_FMT) : 0, tm))) return 0;
			st->want_xday = 1;
			break;
		case 'X':
			if (!(s = fmt_item(st, s, T_FMT, mod == 'E' && nera(st) ? li(st, ERA_T_FMT) : 0, tm))) return 0;
			break;
		case 'y':
			if (mod == 'E') {
				if (!(s = num(s, 0, 9999, 4, &v))) return 0;
				if (st->ec == -2) goto two_digit;
				tm->tm_year = v;
				if (!nera(st)) return 0;
				if (st->ec >= 0 && (!era_at(st, st->ec, &era) || !era_valid(&era, v))) return 0;
				st->have_ey = 1;
				st->ey = v;
				st->want_xday = 1;
				break;
			}
			if (!(s = mod == 'O' ? altnum(st, s, 0, 99, 2, &v) : num(s, 0, 99, 2, &v))) return 0;
		two_digit:
			tm->tm_year = v >= 69 ? v : v + 100;
			st->want_century = 1;
			st->want_xday = 1;
			break;
		case 'Y':
			if (mod == 'E' && nera(st)) {
				unsigned n = nera(st);
				const char *r = 0;
				for (unsigned k = 0; k < n && k < 1000 && !r; k++) {
					if (spend(st)) return 0;
					if (!era_at(st, k, &era)) continue;
					struct st save = *st;
					struct tm tsave = *tm;
					st->ec = k;
					st->have_ey = 0;
					st->in_ey = k;	/* %EC in era k's format names era k */
					r = sub(st, s, era.format, tm);
					st->in_ey = save.in_ey;
					if (!r) *st = save, *tm = tsave;
				}
				if (r) {
					s = r;
					st->want_century = 0;
					st->decided = LOCALE;
					break;
				}
				/* no era format matches: glibc reads %Y */
			}
			if (!(s = num(s, 0, 9999, 4, &v))) return 0;
			tm->tm_year = v - 1900;
			st->want_century = 0;
			st->want_xday = 1;
			break;
		case 'z': {
			s = skipsp(s);
			if (*s == 'Z') {
				s++;
				tm->tm_gmtoff = 0;
				break;
			}
			if (*s != '+' && *s != '-') return 0;
			int neg = *s++ == '-';
			if (s[0] - '0' >= 10U || s[1] - '0' >= 10U) return 0;
			long off = ((s[0] - '0') * 10 + (s[1] - '0')) * 3600L;
			s += 2;
			if (*s == ':' && s[1] - '0' < 10U) s++;
			if (*s - '0' < 10U) {
				if (s[1] - '0' >= 10U) return 0;
				int mm = (s[0] - '0') * 10 + (s[1] - '0');
				if (mm > 59) return 0;
				off += mm * 60L;
				s += 2;
			}
			tm->tm_gmtoff = neg ? -off : off;
			break;
		}
		case 'Z':
			s = skipsp(s);
			while (*s && !isspace((unsigned char)*s)) s++;
			break;
		case '%':
			if (*s++ != '%') return 0;
			break;
		default:
			return 0;
		}
	}
	return s;
}

/* Days before month m (0..25) of a year, the common year's then the leap
 * year's: glibc indexes on past December for a day of the year beyond it. */
static const short mon_yday[26] = {
	0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365,
	0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366,
};

static int leap(long long y)
{
	return y % 4 == 0 && (y % 100 != 0 || y % 400 == 0);
}

/* Day of the week of day mday of month mon (an index into mon_yday's
 * common-year half, which glibc lets run past December) of year y: Julian
 * days with January and February counted in the previous year's leap
 * cycle, with C's truncating division (as glibc's result for years <= 0
 * shows). */
static int dow(long long y, int mon, long long mday)
{
	long long cy = y - (mon < 2);
	long long d = 365 * y + cy / 4 - cy / 100 + cy / 400 + mon_yday[mon] + mday - 1;
	return (int)((d % 7 + 7) % 7);
}

char *strptime(const char *restrict s, const char *restrict f, struct tm *restrict tm)
{
	struct st st;
	memset(&st, 0, sizeof st);
	st.loc = CURRENT_LOCALE;
	st.ec = -1;
	st.in_ey = -1;
	long work = OSV_TIME_WORK;
	st.work = &work;
	s = parse(&st, s, f, tm);
	if (!s || work < 0) return 0;

	if (st.have_I) {
		tm->tm_hour %= 12;
		if (st.is_pm) tm->tm_hour += 12;
	}
	if (st.have_century) {
		if (st.want_century) tm->tm_year = tm->tm_year % 100 + (st.century - 19) * 100;
		else tm->tm_year = (st.century - 19) * 100;
	}
	if (st.ec >= 0 || st.have_ey) {
		struct __osv_era e;
		if (!era_at(&st, st.ec >= 0 ? st.ec : 0, &e)) return 0;
		long long sy = e.start / 10000 - (e.start % 10000 < 0);
		tm->tm_year = (st.have_ey ? era_greg(&e, st.ey) : sy) - 1900;
	}
	long long year = tm->tm_year + 1900LL;
	const short *tab = mon_yday + (leap(year) ? 13 : 0);
	int tab_len = leap(year) ? 13 : 26;
	if (st.want_xday && !st.have_wday) {
		if (!(st.have_mon && st.have_mday) && st.have_yday) {
			int t = 0;
			while (t < tab_len && tab[t] <= tm->tm_yday) t++;
			if (!st.have_mon) tm->tm_mon = t - 1;
			if (!st.have_mday) tm->tm_mday = tm->tm_yday - (t ? tab[t - 1] : 0) + 1;
			st.have_mon = st.have_mday = 1;
		}
		if ((unsigned)tm->tm_mon < (unsigned)tab_len)
			tm->tm_wday = dow(year, tm->tm_mon, tm->tm_mday);
	}
	if (st.want_xday && !st.have_yday && (unsigned)tm->tm_mon < (unsigned)tab_len)
		tm->tm_yday = tab[tm->tm_mon] + tm->tm_mday - 1;
	if ((st.have_uweek || st.have_wweek) && st.have_wday) {
		int save_wday = tm->tm_wday, jan1 = dow(year, 0, 1);
		if (st.have_uweek)
			tm->tm_yday = (7 - jan1) % 7 + (st.week_no - 1) * 7 + save_wday;
		else
			tm->tm_yday = (8 - jan1) % 7 + (st.week_no - 1) * 7 + (save_wday + 6) % 7;
		if (!st.have_mon || !st.have_mday) {
			int t = 0;
			while (t < tab_len && tab[t] <= tm->tm_yday) t++;
			if (!st.have_mon) tm->tm_mon = t - 1;
			if (!st.have_mday) tm->tm_mday = tm->tm_yday - (t ? tab[t - 1] : 0) + 1;
		}
		tm->tm_wday = save_wday;
	}
	return (char *)s;
}
