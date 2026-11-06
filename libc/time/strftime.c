/*
 * This file is an OSv copy of musl's src/time/strftime.c and src/time/wcsftime.c, modified for OSv by
 * Greg Burd in 2026 (glibc-compatible flags, eras, alternative digits and
 * named-locale fallbacks, measured against glibc 2.41).
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

/* strftime, strftime_l, wcsftime and wcsftime_l, one engine for both widths.
 * Behaviour follows glibc 2.41 as measured by running it (no glibc source was
 * read; scripts/locale-oracle/README.md):
 * - flags '_' (pad with spaces), '-' (no padding), '0' (zeros), the last of
 *   them wins; '^' upper-cases the result, '#' swaps the case of names (upper
 *   for %a %A %b %B %h, lower for %p and %Z); a decimal field width;
 *   modifiers E and O, accepted only on the conversions glibc accepts them on;
 * - a number is padded to the larger of its default and the given width
 *   ('-' alone: no padding); %Y %G %C have no padding by default, %z pads the
 *   sign and the digits to the width separately;
 * - strings and composite conversions (%c %x %X %r %R %T %D %F) are padded to
 *   the width, with zeros only for '0'; '^' upper-cases a composite result;
 * - an unknown conversion is copied literally, padded to the width, and
 *   upper-cased by '^' (and by '#' before b or h);
 * - %E: eras from the locale's ERA (%EC %Ey %EY %Ec %Ex %EX); %O: the
 *   locale's ALT_DIGITS for numbers below their count, ALTMON and ABALTMON
 *   for %OB %Ob %Oh;
 * - %r with an empty T_FMT_AMPM is %I:%M:%S %p; %k %l %P %s are supported;
 * - widths are counted in bytes by strftime and in wide characters by
 *   wcsftime; case is mapped with toupper_l (bytes) or towupper_l (wide);
 *   wcsftime's %Z ignores '#', and its width when '-' is given;
 * - the pad flag of a %EY that has one ('_', '-', '0') applies to every later
 *   era year in the same call: %Ey whatever its own flag, %EY without one;
 * - the locale's formats (D_T_FMT, T_FMT, the era formats ...) are data and
 *   may name each other (en_US's T_FMT is "%r"), so their expansion is
 *   bounded: 8 levels deep, and OSV_TIME_WORK steps (a conversion, a
 *   literal run, an era tried) for each conversion of the caller's format;
 *   past that the call fails (returns 0) instead of taking exponential time
 *   on hostile data. Once the output is past the buffer the result is 0 and
 *   the expansion stops. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <langinfo.h>
#include <locale.h>
#include <time.h>
#include <limits.h>
#include <ctype.h>
#include <wchar.h>
#include <wctype.h>
#include <stdint.h>
#include "locale_impl.h"
#include "time_impl.h"
#include "osv-time.h"

#define TIME_ITEM(i) ((LC_TIME << 16) | (i))
#define ALTMON_ITEM(m) TIME_ITEM(111 + (m))
#define ABALTMON_ITEM(m) TIME_ITEM(135 + (m))
#define ERA_NUM_ITEM TIME_ITEM(50)

static int is_leap(int y)
{
	/* Avoid overflow */
	if (y>INT_MAX-1900) y -= 2000;
	y += 1900;
	return !(y%4) && ((y%100) || !(y%400));
}

static int week_num(const struct tm *tm)
{
	int val = (tm->tm_yday + 7U - (tm->tm_wday+6U)%7) / 7;
	/* If 1 Jan is just 1-3 days past Monday,
	 * the previous week is also in this year. */
	if ((tm->tm_wday + 371U - tm->tm_yday - 2) % 7 <= 2)
		val++;
	if (!val) {
		val = 52;
		/* If 31 December of prev year a Thursday,
		 * or Friday of a leap year, then the
		 * prev year has 53 weeks. */
		int dec31 = (tm->tm_wday + 7U - tm->tm_yday - 1) % 7;
		if (dec31 == 4 || (dec31 == 5 && is_leap(tm->tm_year%400-1)))
			val++;
	} else if (val == 53) {
		/* If 1 January is not a Thursday, and not
		 * a Wednesday of a leap year, then this
		 * year has only 52 weeks. */
		int jan1 = (tm->tm_wday + 371U - tm->tm_yday) % 7;
		if (jan1 != 4 && (jan1 != 3 || !is_leap(tm->tm_year)))
			val = 1;
	}
	return val;
}

/* ---- eras (shared with strptime.c) ---- */

static const char *field(const char *p, long *v)
{
	int neg = 0;
	long x = 0;
	if (*p == '-' || *p == '+') neg = *p++ == '-';
	if (*p - '0' >= 10U) return 0;
	for (; *p - '0' < 10U; p++)
		if (x < 100000000) x = 10*x + (*p - '0');
	*v = neg ? -x : x;
	return p;
}

/* "[-]yyyy/mm/dd" as yyyy*10000 + mm*100 + dd (a year "-y" is y-1 before
 * year 0, as there is no year 0 in the era table), or "-*"/"+*". */
static const char *era_date(const char *p, long long *d)
{
	long y, m, dd;
	if ((p[0] == '-' || p[0] == '+') && p[1] == '*') {
		*d = p[0] == '-' ? LLONG_MIN : LLONG_MAX;
		return p + 2;
	}
	if (!(p = field(p, &y)) || *p++ != '/' || !(p = field(p, &m)) ||
	    *p++ != '/' || !(p = field(p, &dd)))
		return 0;
	if (y < 0) y++;
	*d = y * 10000LL + m * 100 + dd;
	return p;
}

/* Parse ERA entry s ("dir:offset:start:end:name:format"). */
int __osv_era_parse(const char *s, struct __osv_era *e)
{
	long off;
	if ((*s != '+' && *s != '-') || s[1] != ':') return 0;
	e->dir = *s;
	s += 2;
	if (!(s = field(s, &off)) || *s++ != ':') return 0;
	if (!(s = era_date(s, &e->start)) || *s++ != ':') return 0;
	if (!(s = era_date(s, &e->end)) || *s++ != ':') return 0;
	e->name = s;
	s = strchr(s, ':');
	if (!s) return 0;
	e->name_len = s - e->name;
	e->format = s + 1;
	e->offset = off;
	return 1;
}

/* The era year of year y in era e (counting away from its start). */
long long __osv_era_year(const struct __osv_era *e, long long y)
{
	long long sy = e->start / 10000 - (e->start % 10000 < 0);
	if (e->start == LLONG_MIN || e->start == LLONG_MAX) return 0;
	return e->offset + (e->start <= e->end ? y - sy : sy - y);
}

/* ---- output ---- */

struct out {
	char *s;
	wchar_t *w;
	size_t n, l;	/* l counts units written or wanted; content valid while l <= n */
	locale_t loc;
	int depth;
	long work;	/* steps left in this conversion; < 0: failed */
	int era_pad;	/* glibc: the pad flag of the last %EY that had one sticks to
			 * every later era year (%Ey, and %EY without a flag) */
};

static int find_era(struct out *o, const struct tm *tm, locale_t loc, struct __osv_era *e)
{
	unsigned n = (unsigned)(uintptr_t)__nl_langinfo_l(ERA_NUM_ITEM, loc);
	long long d = (tm->tm_year + 1900LL) * 10000 + (tm->tm_mon + 1) * 100 + tm->tm_mday;
	for (unsigned i = 0; i < n && i < 1000; i++) {
		if (--o->work < 0) return 0;
		const char *s = __osv_nl_list(ERA, i, loc);
		if (!s) break;
		if (!__osv_era_parse(s, e)) continue;
		long long lo = e->start < e->end ? e->start : e->end;
		long long hi = e->start < e->end ? e->end : e->start;
		if (d >= lo && d <= hi) return 1;
	}
	return 0;
}

static void put1(struct out *o, wchar_t c)
{
	if (o->l < o->n) {
		if (o->w) o->w[o->l] = c;
		else o->s[o->l] = c;
	}
	o->l++;
}

/* Decode one UTF-8 sequence (up to 6 bytes, as the wide format was encoded);
 * a malformed byte stands for itself. */
static size_t dec(const unsigned char *p, size_t k, wchar_t *c)
{
	int n = *p < 0xc0 ? 0 : *p < 0xe0 ? 1 : *p < 0xf0 ? 2 : *p < 0xf8 ? 3 : *p < 0xfc ? 4 : *p < 0xfe ? 5 : 0;
	unsigned v = n ? *p & (0x3f >> n) : *p;
	if (*p >= 0x80 && !n) { *c = *p; return 1; }
	if ((size_t)n >= k) { *c = *p; return 1; }
	for (int i = 1; i <= n; i++) {
		if ((p[i] & 0xc0) != 0x80) { *c = *p; return 1; }
		v = v << 6 | (p[i] & 0x3f);
	}
	*c = v;
	return n + 1;
}

static size_t units(struct out *o, const char *p, size_t k)
{
	if (!o->w) return k;
	size_t u = 0;
	wchar_t c;
	for (size_t i = 0; i < k; u++) i += dec((const unsigned char *)p + i, k - i, &c);
	return u;
}

enum { KEEP, UPPER, LOWER };

static wchar_t map(struct out *o, wchar_t c, int cs)
{
	if (cs == UPPER) return o->w ? (wchar_t)towupper_l(c, o->loc) : toupper_l((unsigned char)c, o->loc);
	if (cs == LOWER) return o->w ? (wchar_t)towlower_l(c, o->loc) : tolower_l((unsigned char)c, o->loc);
	return c;
}

static void put(struct out *o, const char *p, size_t k, int cs)
{
	wchar_t c;
	for (size_t i = 0; i < k; ) {
		if (o->w) i += dec((const unsigned char *)p + i, k - i, &c);
		else c = (unsigned char)p[i++];
		put1(o, map(o, c, cs));
	}
}

static void pad(struct out *o, int ch, size_t k)
{
	for (; k && o->l < o->n; k--) put1(o, ch);
	o->l += k;
}

static void emit_str(struct out *o, const char *p, size_t k, size_t width, int fill, int cs)
{
	size_t u = units(o, p, k);
	if (width > u) pad(o, fill, width - u);
	put(o, p, k, cs);
}

struct spec {
	int pad;	/* 0, '_', '-' or '0' */
	int up, swap;	/* '^', '#' */
	size_t width;	/* 0: none */
	int mod;	/* 0, 'E' or 'O' */
};

/* alt: 1 to use ALT_DIGITS under %O; 2 for %s (no ALT_DIGITS, zero padding
 * before the sign). */
static void emit_num(struct out *o, long long val, size_t defw, int def_fill, const struct spec *sp, int alt)
{
	if (alt == 1 && sp->mod == 'O' && val >= 0 && val < 100) {
		const char *a = __osv_nl_list(ALT_DIGITS, val, o->loc);
		if (a && *a) {
			emit_str(o, a, strlen(a), sp->width, sp->pad == '0' ? '0' : ' ', KEEP);
			return;
		}
	}
	char d[24];
	size_t k = 0;
	int neg = val < 0;
	unsigned long long u = neg ? -(unsigned long long)val : (unsigned long long)val;
	do d[sizeof d - ++k] = '0' + u % 10; while (u /= 10);
	int fill = sp->pad == '_' ? ' ' : sp->pad == '0' ? '0' : sp->pad == '-' ? 0 : def_fill;
	size_t w = fill ? defw : 0;
	if (sp->width > w) w = sp->width;
	if (!fill) fill = ' ';
	size_t len = k + neg;
	if ((fill == ' ' || alt == 2) && w > len) pad(o, fill, w - len);
	if (neg) put1(o, '-');
	if (fill == '0' && alt != 2 && w > len) pad(o, '0', w - len);
	put(o, d + sizeof d - k, k, KEEP);
}

static void core(struct out *o, const char *f, const struct tm *tm, int inh_pad);

/* A conversion whose result is the format fmt: pad and case the result. */
static void composite(struct out *o, const char *fmt, const struct tm *tm, const struct spec *sp, int inh_pad)
{
	size_t l0 = o->l;
	if (o->depth > 8) return;
	o->depth++;
	core(o, fmt, tm, inh_pad);
	o->depth--;
	if (o->work < 0) return;
	if (o->l > o->n) {
		if (sp->width > o->l - l0) o->l = l0 + sp->width;
		return;
	}
	if (sp->up)
		for (size_t i = l0; i < o->l; i++) {
			if (o->w) o->w[i] = map(o, o->w[i], UPPER);
			else o->s[i] = map(o, (unsigned char)o->s[i], UPPER);
		}
	size_t len = o->l - l0;
	if (sp->width > len) {
		size_t k = sp->width - len;
		int fill = sp->pad == '0' ? '0' : ' ';
		if (o->l + k > o->n) {
			o->l += k;
			return;
		}
		if (o->w) {
			memmove(o->w + l0 + k, o->w + l0, len * sizeof *o->w);
			for (size_t i = 0; i < k; i++) o->w[l0 + i] = fill;
		} else {
			memmove(o->s + l0 + k, o->s + l0, len);
			memset(o->s + l0, fill, k);
		}
		o->l += k;
	}
}

static const char *li(nl_item it, locale_t loc)
{
	return __nl_langinfo_l(it, loc);
}

/* One conversion; 0 if c is not one. */
static int conv(struct out *o, int c, const struct tm *tm, const struct spec *sp)
{
	locale_t loc = o->loc;
	const char *str;
	int cs = KEEP, fill = sp->pad == '0' ? '0' : ' ';
	long long val, year = tm->tm_year + 1900LL;
	size_t defw = 2;
	int def_fill = '0';
	struct __osv_era era;
	struct tm tmp;

	if (sp->mod == 'E' && !strchr("cCnpPrRstTuxXyYzZ%", c)) return 0;
	if (sp->mod == 'O' && !strchr("bBCdegGhHIjklmMnpPrRsStTuUVwWyzZ%", c)) return 0;
	switch (c) {
	case 'a':
		if (tm->tm_wday > 6U) { str = "?"; goto string; }
		str = li(ABDAY_1 + tm->tm_wday, loc);
		goto name;
	case 'A':
		if (tm->tm_wday > 6U) { str = "?"; goto string; }
		str = li(DAY_1 + tm->tm_wday, loc);
		goto name;
	case 'h':
	case 'b':
		if (tm->tm_mon > 11U) { str = "?"; goto string; }
		str = li(sp->mod == 'O' ? ABALTMON_ITEM(tm->tm_mon) : ABMON_1 + tm->tm_mon, loc);
		goto name;
	case 'B':
		if (tm->tm_mon > 11U) { str = "?"; goto string; }
		str = li(sp->mod == 'O' ? ALTMON_ITEM(tm->tm_mon) : MON_1 + tm->tm_mon, loc);
		goto name;
	case 'c':
		str = sp->mod == 'E' ? li(ERA_D_T_FMT, loc) : "";
		if (!*str) str = li(D_T_FMT, loc);
		goto comp;
	case 'C':
		if (sp->mod == 'E' && find_era(o, tm, loc, &era)) {
			emit_str(o, era.name, era.name_len, sp->width, fill, sp->up ? UPPER : KEEP);
			return 1;
		}
		val = year / 100 - (year % 100 < 0);
		defw = 1;
		goto number;
	case 'e':
		def_fill = ' ';
	case 'd':
		val = tm->tm_mday;
		goto number;
	case 'D':
		str = "%m/%d/%y";
		goto comp;
	case 'F':
		str = "%Y-%m-%d";
		goto comp;
	case 'g':
	case 'G':
		val = year;
		if (tm->tm_yday < 3 && week_num(tm) != 1) val--;
		else if (tm->tm_yday > 360 && week_num(tm) == 1) val++;
		if (c == 'g') val = (val % 100 + 100) % 100;
		else defw = 1;
		goto number;
	case 'k':
		def_fill = ' ';
	case 'H':
		val = tm->tm_hour;
		goto number;
	case 'l':
		def_fill = ' ';
	case 'I':
		val = tm->tm_hour % 12;
		if (!val) val = 12;
		goto number;
	case 'j':
		val = tm->tm_yday + 1;
		defw = 3;
		goto number;
	case 'm':
		val = tm->tm_mon + 1;
		goto number;
	case 'M':
		val = tm->tm_min;
		goto number;
	case 'n':
		str = "\n";
		goto string;
	case 't':
		str = "\t";
		goto string;
	case 'p':
	case 'P':
		str = li(tm->tm_hour >= 12 ? PM_STR : AM_STR, loc);
		cs = c == 'P' || sp->swap ? LOWER : sp->up ? UPPER : KEEP;
		goto string;
	case 'r':
		str = li(T_FMT_AMPM, loc);
		if (!*str) str = "%I:%M:%S %p";
		goto comp;
	case 'R':
		str = "%H:%M";
		goto comp;
	case 's':
		tmp = *tm;
		val = mktime(&tmp);
		emit_num(o, val, 1, ' ', sp, 2);
		return 1;
	case 'S':
		val = tm->tm_sec;
		goto number;
	case 'T':
		str = "%H:%M:%S";
		goto comp;
	case 'u':
		val = tm->tm_wday ? tm->tm_wday : 7;
		defw = 1;
		goto number;
	case 'U':
		val = (tm->tm_yday + 7U - tm->tm_wday) / 7;
		goto number;
	case 'W':
		val = (tm->tm_yday + 7U - (tm->tm_wday+6U)%7) / 7;
		goto number;
	case 'V':
		val = week_num(tm);
		goto number;
	case 'w':
		val = tm->tm_wday;
		defw = 1;
		goto number;
	case 'x':
		str = sp->mod == 'E' ? li(ERA_D_FMT, loc) : "";
		if (!*str) str = li(D_FMT, loc);
		goto comp;
	case 'X':
		str = sp->mod == 'E' ? li(ERA_T_FMT, loc) : "";
		if (!*str) str = li(T_FMT, loc);
		goto comp;
	case 'y':
		if (sp->mod == 'E' && find_era(o, tm, loc, &era)) {
			struct spec s2 = *sp;
			if (o->era_pad) s2.pad = o->era_pad;
			val = __osv_era_year(&era, year);
			emit_num(o, val, 2, '0', &s2, 0);
			return 1;
		}
		val = (year % 100 + 100) % 100;
		goto number;
	case 'Y':
		if (sp->mod == 'E' && find_era(o, tm, loc, &era)) {
			if (sp->pad) o->era_pad = sp->pad;
			composite(o, era.format, tm, sp, o->era_pad);
			return 1;
		}
		val = year;
		defw = 1;
		goto number;
	case 'z':
		if (tm->tm_isdst < 0) return 1;
		val = tm->__tm_gmtoff;
		str = val < 0 ? "-" : "+";
		if (val < 0) val = -val;
		emit_str(o, str, 1, sp->width, fill, KEEP);
		val = val / 3600 * 100 + val % 3600 / 60;
		defw = 4;
		goto number;
	case 'Z':
		if (tm->tm_isdst < 0) return 1;
		str = __tm_to_tzname(tm);
		if (o->w) {
			/* glibc's wcsftime: '#' does not change %Z, and '-'
			 * drops its width. */
			cs = sp->up ? UPPER : KEEP;
			emit_str(o, str, strlen(str), sp->pad == '-' ? 0 : sp->width, fill, cs);
			return 1;
		}
		cs = sp->swap ? LOWER : sp->up ? UPPER : KEEP;
		goto string;
	case '%':
		str = "%";
		goto string;
	default:
		return 0;
	}
number:
	emit_num(o, val, defw, def_fill, sp, 1);
	return 1;
name:
	cs = sp->up || sp->swap ? UPPER : KEEP;
string:
	emit_str(o, str, strlen(str), sp->width, fill, cs);
	return 1;
comp:
	composite(o, str, tm, sp, 0);
	return 1;
}

static void core(struct out *o, const char *f, const struct tm *tm, int inh_pad)
{
	while (*f) {
		const char *p = f;
		if (!o->depth && o->work >= 0) o->work = OSV_TIME_WORK;
		if (o->l >= o->n || --o->work < 0) return;	/* the result is 0 */
		if (*f != '%') {
			while (*f && *f != '%') f++;
			put(o, p, f - p, KEEP);
			continue;
		}
		struct spec sp = { inh_pad, 0, 0, 0, 0 };
		for (f++; ; f++) {
			if (*f == '_' || *f == '-' || *f == '0') sp.pad = *f;
			else if (*f == '^') sp.up = 1;
			else if (*f == '#') sp.swap = 1;
			else break;
		}
		for (; *f - '0' < 10U; f++)
			if (sp.width < 100000000) sp.width = 10 * sp.width + (*f - '0');
		if (*f == 'E' || *f == 'O') sp.mod = *f++;
		if (!*f || !conv(o, *f, tm, &sp)) {
			/* Not a conversion: copied as written, '^' (and '#' before
			 * b or h) upper-casing it, as glibc does. */
			int up = sp.up || (sp.swap && (*f == 'b' || *f == 'h'));
			if (*f) f++;
			/* wcsftime: the whole character (its UTF-8 sequence) */
			if (o->w) while ((*f & 0xc0) == 0x80) f++;
			emit_str(o, p, f - p, sp.width, sp.pad == '0' ? '0' : ' ', up ? UPPER : KEEP);
			continue;
		}
		f++;
	}
}

size_t __strftime_l(char *restrict s, size_t n, const char *restrict f, const struct tm *restrict tm, locale_t loc)
{
	struct out o = { s, 0, n, 0, loc, 0, OSV_TIME_WORK, 0 };
	core(&o, f, tm, 0);
	if (o.l >= n || o.work < 0) return 0;
	s[o.l] = 0;
	return o.l;
}

size_t strftime(char *restrict s, size_t n, const char *restrict f, const struct tm *restrict tm)
{
	return __strftime_l(s, n, f, tm, CURRENT_LOCALE);
}

weak_alias(__strftime_l, strftime_l);

size_t __wcsftime_l(wchar_t *restrict s, size_t n, const wchar_t *restrict f, const struct tm *restrict tm, locale_t loc)
{
	/* Run the engine on the format encoded as UTF-8 (any wchar_t value,
	 * up to six bytes, decoded back exactly by dec()). */
	size_t k = wcslen(f), j = 0;
	if (k > (SIZE_MAX - 1) / 6) return 0;
	char *mb = malloc(6 * k + 1);
	if (!mb) return 0;
	for (size_t i = 0; i < k; i++) {
		unsigned c = f[i];
		int m = c < 0x80 ? 0 : c < 0x800 ? 1 : c < 0x10000 ? 2 : c < 0x200000 ? 3 : c < 0x4000000 ? 4 : 5;
		if (!m) { mb[j++] = c; continue; }
		mb[j++] = ((0xff << (7 - m)) & 0xff) | c >> (6 * m);
		for (int b = m - 1; b >= 0; b--) mb[j++] = 0x80 | (c >> (6 * b) & 0x3f);
	}
	mb[j] = 0;
	struct out o = { 0, s, n, 0, loc, 0, OSV_TIME_WORK, 0 };
	core(&o, mb, tm, 0);
	free(mb);
	if (o.l >= n || o.work < 0) return 0;
	s[o.l] = 0;
	return o.l;
}

size_t wcsftime(wchar_t *restrict wcs, size_t n, const wchar_t *restrict f, const struct tm *restrict tm)
{
	return __wcsftime_l(wcs, n, f, tm, CURRENT_LOCALE);
}

weak_alias(__wcsftime_l, wcsftime_l);
