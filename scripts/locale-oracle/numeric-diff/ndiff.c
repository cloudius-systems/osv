/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
/* Host differential: glibc's printf, scanf and strtod family against OSv's
 * libc/stdio and libc/internal sources built for Linux (build.sh) as
 * osv_..., with the same compiled locale data (LOCPATH):
 *   ndiff <locale>...   prints the cases that differ and a SUMMARY; exit 1 if any.
 * printf: every conversion with every subset of the flags ' - + space # 0
 * (and I), widths, precisions, * and .*, over a value set; snprintf at
 * buffer sizes 0..; swprintf. scanf/strtod: inputs built from the
 * locale's own decimal point and separator, and the other one, grouped and
 * not, leading space, hex floats, inf/nan, exponents; every float and
 * integer conversion with and without ', with widths; sscanf and swscanf;
 * strtod, strtof, strtold and their _l variants. */
#define _GNU_SOURCE
#include <float.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

int osv_snprintf(char *, size_t, const char *, ...);
int osv_swprintf(wchar_t *, size_t, const wchar_t *, ...);
int osv_sscanf(const char *, const char *, ...);
int osv_swscanf(const wchar_t *, const wchar_t *, ...);
double osv_strtod(const char *, char **);
float osv_strtof(const char *, char **);
long double osv_strtold(const char *, char **);
double osv_strtod_l(const char *, char **, locale_t);
float osv_strtof_l(const char *, char **, locale_t);
long double osv_strtold_l(const char *, char **, locale_t);

static long ndiff, ntot, nhex, npre, nglibci, maxshow = 300;
static const char *cur;
/* cid: the case number (one printf call, one scanf format on one input,
 * one strto call), the same sequence in every locale.
 * C and C.UTF-8: every diff is a C-DIFF, written to $NDIFF_CLIST as
 * "cid what format" so that build.sh base can show the same cases differ
 * at the Stage 1a base (they are musl/glibc differences 1b does not
 * touch). A named locale's diff is PRE if the same case differed in C
 * with the same results on both sides once the locale's decimal point is
 * read as '.' and its separator dropped (so the locale changed nothing in
 * it); PRE needs C to be the first locale given. */
static long cid, key;
static int in_c;
static long nc;
static FILE *clist;
#define CTAB 65536
static struct { long cid; char *g, *o; } ctab[CTAB];
static char *norm(const char *s)
{
	const struct lconv *lc = localeconv();
	size_t ld = strlen(lc->decimal_point), lt = strlen(lc->thousands_sep);
	char *r = malloc(strlen(s) + 1), *q = r;
	while (*s) {
		if (lt && !strncmp(s, lc->thousands_sep, lt)) s += lt;
		else if (ld && !strncmp(s, lc->decimal_point, ld)) *q++ = '.', s += ld;
		else *q++ = *s++;
	}
	*q = 0;
	return r;
}
/* OSVHASH: FNV-1a over OSv's results of every case outside the new
 * features (scanf's ' and the I flag), equal for C and C.UTF-8 between
 * this tree and the Stage 1a base (ndiffbase) if their behaviour there is
 * unchanged */
static uint64_t osvhash;
static int hashable(const char *fmt, int scan)
{
	return !strchr(fmt, 'I') && !(scan && strchr(fmt, '\''));
}
static void hash(const void *p, size_t n)
{
	const unsigned char *b = p;
	while (n--) osvhash = (osvhash ^ *b++) * 1099511628211ULL;
}

/* %a/%A of a denormal or a long double: musl writes the leading hex digit
 * as 1 and glibc does not (0x1p-1074 vs 0x0.0000000000001p-1022, 0x1.2dp+20
 * vs 0x9.6bp+17); the same value, a musl/glibc difference that predates
 * Stage 1b and is not about the locale. Counted on their own (KNOWN-HEX). */
static int known_hex(const char *what, const char *fmt, const char *g, const char *o)
{
	size_t n = strlen(fmt);
	char c = n ? fmt[n - 1 - (n > 1 && fmt[n - 1] == '|')] : 0;
	if (strncmp(what, "printf", 6) && strncmp(what, "wprintf", 7)) return 0;
	if ((c | 32) != 'a') return 0;
	return strstr(fmt, "L") || strstr(g, "p-1022") || strstr(g, "P-1022");
}

/* musl's scanf (before and after 1b, in every locale: the C ones are
 * C-DIFF at the base too) fails a float whose exponent, hex prefix or
 * nan(...) is cut by the field width or the input's end ("1.5e" %lf:
 * glibc 1.5, musl no match), where glibc takes the number before it
 * (counted as MUSL-PARTIAL). Recognised here when glibc matched with a
 * value and OSv did not, and the input has an e, p, x or nan( within the
 * width glibc consumed plus one more. */
static long npartial, noverflow;
static int showcls;	/* NDIFF_SHOWCLASS: also print the MUSL-* cases */
static int musl_partial(const char *what, const char *in, const char *fmt, const char *g, const char *o)
{
	int rg, ro, ng, no;
	char vg[64], vo[64], ig[64], io[64];
	if (!strstr(what, "scanf")) return 0;
	if (sscanf(g, "%d %63s %63s +%d", &rg, vg, ig, &ng) != 4 || sscanf(o, "%d %63s %63s +%d", &ro, vo, io, &no) != 4)
		return 0;
	/* glibc matched (or, with *, consumed: %n set) and OSv did not */
	if (!(ro == 0 && no == -7 && (rg >= 1 || ng >= 0))) return 0;
	int w = 0;
	const char *p = strchr(fmt, '%');
	while (p && *++p && strchr("'*", *p));
	if (p) w = atoi(p);
	while (*in == ' ' || (*in >= '\t' && *in <= '\r')) in++;	/* not counted in the width */
	/* the width counts bytes for sscanf, characters for swscanf */
	int wide = !strncmp(what, "swscanf", 7);
	for (int k = 0; *in && (!w || k <= w); in++) {
		if (wide && ((unsigned char)*in & 0xc0) == 0x80) continue;
		if (strchr("eEpPxX(", *in)) return 1;
		k++;
	}
	return 0;
}

/* musl's scanf stores an overflowing integer as ULLONG_MAX cut to the
 * type (-1), glibc saturates it to the type's limit; at the Stage 1a base
 * and in C alike (C-DIFF), counted as MUSL-OVERFLOW */
static int musl_overflow(const char *what, const char *g, const char *o)
{
	int rg, ro, ng, no;
	long long vg, vo;
	if (!strstr(what, "scanf")) return 0;
	if (sscanf(g, "%d %lld %*s +%d", &rg, &vg, &ng) != 3 || sscanf(o, "%d %lld %*s +%d", &ro, &vo, &no) != 3)
		return 0;
	return rg == ro && ng == no && (vg == LLONG_MAX || vg == LLONG_MIN || vg == INT_MAX || vg == INT_MIN ||
					vg == SHRT_MAX || vg == SHRT_MIN) && vo != vg;
}

static void report(const char *what, const char *fmt, const char *g, const char *o)
{
	if (known_hex(what, fmt, g, o)) {
		nhex++;
		return;
	}
	/* glibc 2.41 bug: narrow %I on a float in a locale whose LC_CTYPE has
	 * no outdigits (C) drops every digit ("%If" -> "."; probe1); OSv
	 * writes ASCII digits, as glibc's wide printf and C.UTF-8 do */
	if (!strcmp(cur, "C") && strchr(fmt, 'I') && !strncmp(what, "printf", 6)) {
		const char *c = fmt + strlen(fmt) - 1 - (fmt[strlen(fmt) - 1] == '|');
		if (strchr("fFeEgG", *c) && !strpbrk(g, "0123456789")) {
			nglibci++;
			return;
		}
	}
	if (in_c) {
		nc++;
		if (clist) fprintf(clist, "%s %ld %.*s [%s]\n", cur, cid, (int)strcspn(what, " "), what, fmt);
		if (!strcmp(cur, "C")) {
			unsigned h = (unsigned)key * 2654435761u % CTAB;
			while (ctab[h].g) h = (h + 1) % CTAB;
			ctab[h].cid = key; ctab[h].g = strdup(g); ctab[h].o = strdup(o);
		}
		if (nc <= maxshow) printf("C-DIFF %s #%ld %s [%s] glibc [%s] osv [%s]\n", cur, cid, what, fmt, g, o);
		return;
	}
	{
		unsigned h = (unsigned)key * 2654435761u % CTAB;
		for (; ctab[h].g; h = (h + 1) % CTAB)
			if (ctab[h].cid == key) {
				char *ng = norm(g), *no = norm(o);
				int same = !strcmp(ng, ctab[h].g) && !strcmp(no, ctab[h].o);
				free(ng); free(no);
				if (same) {
					npre++;
					return;
				}
				break;
			}
	}
	{
		const char *in = strstr(what, "in=[");
		char buf[300];
		if (in) {
			snprintf(buf, sizeof buf, "%s", in + 4);
			if (*buf) buf[strlen(buf) - 1] = 0;
			if (musl_partial(what, buf, fmt, g, o)) {
				npartial++;
				if (showcls) printf("MUSL-PARTIAL %s #%ld %s [%s] glibc [%s] osv [%s]\n", cur, cid, what, fmt, g, o);
				return;
			}
			if (musl_overflow(what, g, o)) {
				noverflow++;
				if (showcls) printf("MUSL-OVERFLOW %s #%ld %s [%s] glibc [%s] osv [%s]\n", cur, cid, what, fmt, g, o);
				return;
			}
		}
	}
	ndiff++;
	if (ndiff <= maxshow) printf("DIFF %s #%ld %s [%s] glibc [%s] osv [%s]\n", cur, cid, what, fmt, g, o);
}

/* printf of one format and argument, at several buffer sizes, narrow and wide */
#define PF(fmt, ...) do { \
	char a[2048], b[2048]; \
	cid++; \
	int ra = snprintf(a, sizeof a, fmt, __VA_ARGS__); \
	int rb = osv_snprintf(b, sizeof b, fmt, __VA_ARGS__); \
	if (hashable(fmt, 0)) { hash(&rb, sizeof rb); hash(b, strlen(b)); } \
	ntot++; \
	key = cid * 8; \
	if (ra != rb || strcmp(a, b)) report("printf", fmt, a, b); \
	else if (ra > 0 && ra < 2048) { \
		size_t ns[] = { 0, 1, (size_t)ra / 2, (size_t)ra, (size_t)ra + 1 }; \
		for (int q = 0; q < 5; q++) { \
			memset(a, 'Z', ra + 2); memset(b, 'Z', ra + 2); \
			int x = snprintf(a, ns[q], fmt, __VA_ARGS__), y = osv_snprintf(b, ns[q], fmt, __VA_ARGS__); \
			ntot++; \
			key = cid * 8 + 1 + q; \
			if (x != y || memcmp(a, b, ra + 2)) report("printf-n", fmt, a, b); \
		} \
	} \
	wchar_t wf[64], wa[1024], wb[1024]; \
	mbstowcs(wf, fmt, 64); \
	wmemset(wa, 0, 1024); wmemset(wb, 0, 1024); \
	ra = swprintf(wa, 1024, wf, __VA_ARGS__); \
	rb = osv_swprintf(wb, 1024, wf, __VA_ARGS__); \
	if (hashable(fmt, 0)) { hash(&rb, sizeof rb); hash(wb, wcslen(wb) * sizeof *wb); } \
	ntot++; \
	key = cid * 8 + 7; \
	if (ra != rb || wcscmp(wa, wb)) { \
		char x[2048], y[2048]; \
		snprintf(x, sizeof x, "%d %ls", ra, wa); snprintf(y, sizeof y, "%d %ls", rb, wb); \
		report("wprintf", fmt, x, y); \
	} \
} while (0)

static const char *flagsets[64];
static void make_flagsets(void)
{
	static char buf[64][8];
	const char fl[] = "'-+ #0";
	for (int m = 0; m < 64; m++) {
		char *p = buf[m];
		for (int i = 0; i < 6; i++) if (m >> i & 1) *p++ = fl[i];
		*p = 0;
		flagsets[m] = buf[m];
	}
}

static void test_printf(void)
{
	static const double dv[] = { 0, -0.0, 1, -1, 0.5, 1234567.891, -1234567.891, 12345.25, 999.5, 1e300, 1e-300,
		123456789012.0, 0.000123, 9.9999995, 1e15, 1e16, 1e21, DBL_MAX, DBL_MIN, 5e-324, INFINITY, -INFINITY, NAN, 100, 1000 };
	static const long long iv[] = { 0, 1, -1, 7, 999, 1000, 12345, -12345, 1234567, INT_MIN, INT_MAX, LLONG_MAX, LLONG_MIN,
		100000, 1000000000000LL };
	static const char *widths[] = { "", "1", "8", "15", "30" };
	static const char *precs[] = { "", ".", ".0", ".1", ".3", ".10", ".20" };
	static const char fconv[] = "fFeEgGaA";
	static const char iconv[] = "diouxX";
	char fmt[64];
	for (int m = 0; m < 64; m++)
		for (unsigned w = 0; w < sizeof widths / sizeof *widths; w++)
			for (unsigned p = 0; p < sizeof precs / sizeof *precs; p++) {
				for (const char *c = fconv; *c; c++) {
					snprintf(fmt, sizeof fmt, "%%%s%s%s%c", flagsets[m], widths[w], precs[p], *c);
					for (unsigned k = 0; k < sizeof dv / sizeof *dv; k++) PF(fmt, dv[k]);
					snprintf(fmt, sizeof fmt, "%%%s%s%sL%c", flagsets[m], widths[w], precs[p], *c);
					PF(fmt, (long double)1234567.891);
					PF(fmt, -12345.678L);
				}
				for (const char *c = iconv; *c; c++) {
					snprintf(fmt, sizeof fmt, "%%%s%s%sll%c", flagsets[m], widths[w], precs[p], *c);
					for (unsigned k = 0; k < sizeof iv / sizeof *iv; k++) PF(fmt, iv[k]);
					snprintf(fmt, sizeof fmt, "%%%s%s%s%c", flagsets[m], widths[w], precs[p], *c);
					PF(fmt, 1234567);
					PF(fmt, -42);
					snprintf(fmt, sizeof fmt, "%%%s%s%shh%c", flagsets[m], widths[w], precs[p], *c);
					PF(fmt, 200);
				}
			}
	/* I (LC_CTYPE outdigits) with the other flags, and * / .* */
	static const char *ifm[] = { "%Id", "%I'd", "%'Id", "%I10d", "%I-10d|", "%I010d", "%I.6d", "%I+d", "%Iu", "%Ix", "%Io",
		"%If", "%I'f", "%I20f|", "%I020f", "%I-20f|", "%Ie", "%I020e", "%Ig", "%I#g", "%I'020g", "%Ia", "%I020a", "%IE",
		"%'I.0f", "%I.3f", "%Ilf", "%ILf" };
	for (unsigned k = 0; k < sizeof ifm / sizeof *ifm; k++) {
		const char *c = ifm[k] + strlen(ifm[k]) - 1 - (ifm[k][strlen(ifm[k]) - 1] == '|');
		if (strchr("diuxo", *c)) { PF(ifm[k], 1234567); PF(ifm[k], -905); PF(ifm[k], 0); }
		else if (strstr(ifm[k], "L")) PF(ifm[k], 1234567.891L);
		else { PF(ifm[k], 1234567.891); PF(ifm[k], -0.0625); PF(ifm[k], 1e300); }
	}
	for (int w = -25; w <= 25; w += 5)
		for (int p = -1; p <= 12; p += 3) {
			PF("%'*.*f|", w, p, 1234567.891);
			PF("%'*.*d|", w, p, -1234567);
			PF("%*.*e|", w, p, 1234567.891);
			PF("%'*.*g|", w, p, 12345.678);
		}
	PF("%s %'d %s %'.2f %%", "a", 1234567, "b", 7654321.125);
	PF("%2$'d %1$'.3f", 1234.5, 98765);
	PF("%'d%'d", 1000, 2000);
}

/* one input through every float and integer conversion of scanf */
static unsigned tmpl;	/* the input template's index, in the reports */
static void scan_one(const char *in)
{
	static const char *ff[] = { "%lf%n", "%'lf%n", "%f%n", "%'f%n", "%Lf%n", "%'Lf%n", "%le%n", "%lg%n", "%la%n",
		"%3lf%n", "%4lf%n", "%5lf%n", "%7lf%n", "%'3lf%n", "%'4lf%n", "%'5lf%n", "%'6lf%n", "%'7lf%n", "%'9lf%n",
		"%'*lf%n", "%*'lf%n", "%lf%lf%n", "%'lf,%n", "%lf.%n" };
	static const char *fi[] = { "%d%n", "%'d%n", "%'i%n", "%'u%n", "%'x%n", "%'o%n", "%'ld%n", "%'lld%n", "%'hd%n",
		"%'2d%n", "%'3d%n", "%'4d%n", "%'5d%n", "%'6d%n", "%'7d%n", "%'9d%n", "%i%n", "%'d%d%n" };
	wchar_t w[256], wf[64];
	/* an input that is not valid UTF-8 (a lone first byte of a multibyte
	 * item) goes to swscanf as one character per byte */
	if (mbstowcs(w, in, 256) == (size_t)-1) {
		size_t i;
		for (i = 0; in[i] && i < 255; i++) w[i] = (unsigned char)in[i];
		w[i] = 0;
	}
	for (unsigned k = 0; k < sizeof ff / sizeof *ff; k++) {
		long double la = -7, lb = -7;
		double da = -7, db = -7, da2 = -7, db2 = -7;
		float fa = -7, fb = -7;
		int na = -7, nb = -7, ra, rb;
		const char *f = ff[k];
		cid++;
		if (strstr(f, "%L") || strstr(f, "%'L")) {
			ra = sscanf(in, f, &la, &na); rb = osv_sscanf(in, f, &lb, &nb);
			da = la; db = lb;
		} else if (strstr(f, "%f") || strstr(f, "%'f")) {
			ra = sscanf(in, f, &fa, &na); rb = osv_sscanf(in, f, &fb, &nb);
			da = fa; db = fb;
		} else if (strstr(f, "*")) {
			ra = sscanf(in, f, &na); rb = osv_sscanf(in, f, &nb);
		} else if (strstr(f, "%lf%lf")) {
			ra = sscanf(in, f, &da, &da2, &na); rb = osv_sscanf(in, f, &db, &db2, &nb);
		} else {
			ra = sscanf(in, f, &da, &na); rb = osv_sscanf(in, f, &db, &nb);
		}
		ntot++;
		key = cid * 8;
		if (ra != rb || na != nb || memcmp(&da, &db, sizeof da) || memcmp(&da2, &db2, sizeof da2)) {
			char x[128], y[128];
			snprintf(x, 128, "%d %a %a +%d", ra, da, da2, na); snprintf(y, 128, "%d %a %a +%d", rb, db, db2, nb);
			char what[300]; snprintf(what, 300, "sscanf t%u in=[%s]", tmpl, in);
			report(what, f, x, y);
		}
		if (hashable(f, 1)) { hash(&rb, sizeof rb); hash(&nb, sizeof nb); hash(&db, sizeof db); hash(&db2, sizeof db2); }
		cid++;
		mbstowcs(wf, f, 64);
		la = lb = -7; da = db = da2 = db2 = -7; fa = fb = -7; na = nb = -7;
		if (strstr(f, "%L") || strstr(f, "%'L")) {
			ra = swscanf(w, wf, &la, &na); rb = osv_swscanf(w, wf, &lb, &nb);
			da = la; db = lb;
		} else if (strstr(f, "%f") || strstr(f, "%'f")) {
			ra = swscanf(w, wf, &fa, &na); rb = osv_swscanf(w, wf, &fb, &nb);
			da = fa; db = fb;
		} else if (strstr(f, "*")) {
			ra = swscanf(w, wf, &na); rb = osv_swscanf(w, wf, &nb);
		} else if (strstr(f, "%lf%lf")) {
			ra = swscanf(w, wf, &da, &da2, &na); rb = osv_swscanf(w, wf, &db, &db2, &nb);
		} else {
			ra = swscanf(w, wf, &da, &na); rb = osv_swscanf(w, wf, &db, &nb);
		}
		ntot++;
		key = cid * 8;
		if (ra != rb || na != nb || memcmp(&da, &db, sizeof da) || memcmp(&da2, &db2, sizeof da2)) {
			char x[128], y[128];
			snprintf(x, 128, "%d %a %a +%d", ra, da, da2, na); snprintf(y, 128, "%d %a %a +%d", rb, db, db2, nb);
			char what[300]; snprintf(what, 300, "swscanf t%u in=[%s]", tmpl, in);
			report(what, f, x, y);
		}
		if (hashable(f, 1)) { hash(&rb, sizeof rb); hash(&nb, sizeof nb); hash(&db, sizeof db); hash(&db2, sizeof db2); }
	}
	for (unsigned k = 0; k < sizeof fi / sizeof *fi; k++) {
		long long a = -7, b = -7, a2 = -7, b2 = -7;
		int na = -7, nb = -7, ra, rb;
		const char *f = fi[k];
		cid++;
		int ll = strstr(f, "ll") != 0, l = !ll && strstr(f, "l") != 0, h = strstr(f, "h") != 0;
		if (ll || l) { ra = sscanf(in, f, &a, &na); rb = osv_sscanf(in, f, &b, &nb); }
		else if (h) { short x = -7, y = -7; ra = sscanf(in, f, &x, &na); rb = osv_sscanf(in, f, &y, &nb); a = x; b = y; }
		else if (strstr(f, "%d%n") && strstr(f, "'d%d")) {
			int x = -7, y = -7, x2 = -7, y2 = -7;
			ra = sscanf(in, f, &x, &x2, &na); rb = osv_sscanf(in, f, &y, &y2, &nb); a = x; b = y; a2 = x2; b2 = y2;
		} else { int x = -7, y = -7; ra = sscanf(in, f, &x, &na); rb = osv_sscanf(in, f, &y, &nb); a = x; b = y; }
		ntot++;
		key = cid * 8;
		if (ra != rb || na != nb || a != b || a2 != b2) {
			char x[128], y[128];
			snprintf(x, 128, "%d %lld %lld +%d", ra, a, a2, na); snprintf(y, 128, "%d %lld %lld +%d", rb, b, b2, nb);
			char what[300]; snprintf(what, 300, "sscanf t%u in=[%s]", tmpl, in);
			report(what, f, x, y);
		}
		if (hashable(f, 1)) { hash(&rb, sizeof rb); hash(&nb, sizeof nb); hash(&b, sizeof b); hash(&b2, sizeof b2); }
		cid++;
		mbstowcs(wf, f, 64);
		a = b = a2 = b2 = -7; na = nb = -7;
		if (ll || l) { ra = swscanf(w, wf, &a, &na); rb = osv_swscanf(w, wf, &b, &nb); }
		else if (h) { short x = -7, y = -7; ra = swscanf(w, wf, &x, &na); rb = osv_swscanf(w, wf, &y, &nb); a = x; b = y; }
		else if (strstr(f, "'d%d")) {
			int x = -7, y = -7, x2 = -7, y2 = -7;
			ra = swscanf(w, wf, &x, &x2, &na); rb = osv_swscanf(w, wf, &y, &y2, &nb); a = x; b = y; a2 = x2; b2 = y2;
		} else { int x = -7, y = -7; ra = swscanf(w, wf, &x, &na); rb = osv_swscanf(w, wf, &y, &nb); a = x; b = y; }
		ntot++;
		key = cid * 8;
		if (ra != rb || na != nb || a != b || a2 != b2) {
			char x[128], y[128];
			snprintf(x, 128, "%d %lld %lld +%d", ra, a, a2, na); snprintf(y, 128, "%d %lld %lld +%d", rb, b, b2, nb);
			char what[300]; snprintf(what, 300, "swscanf t%u in=[%s]", tmpl, in);
			report(what, f, x, y);
		}
		if (hashable(f, 1)) { hash(&rb, sizeof rb); hash(&nb, sizeof nb); hash(&b, sizeof b); hash(&b2, sizeof b2); }
	}
}

static void strto_one(const char *in, locale_t l)
{
	char *ea, *eb;
	cid++;
	double a = strtod(in, &ea), b = osv_strtod(in, &eb);
	hash(&b, sizeof b); hash(&eb, 0); { long n = eb - in; hash(&n, sizeof n); }
	ntot++;
	key = cid * 8;
	if (memcmp(&a, &b, sizeof a) || ea != eb) {
		char x[128], y[128]; snprintf(x, 128, "%a +%d", a, (int)(ea - in)); snprintf(y, 128, "%a +%d", b, (int)(eb - in));
		report("strtod", in, x, y);
	}
	float fa = strtof(in, &ea), fb = osv_strtof(in, &eb);
	cid++; hash(&fb, sizeof fb); { long n = eb - in; hash(&n, sizeof n); }
	ntot++;
	key = cid * 8;
	if (memcmp(&fa, &fb, sizeof fa) || ea != eb) report("strtof", in, "", "");
	long double la = strtold(in, &ea), lb = osv_strtold(in, &eb);
	cid++; { double d = lb; long n = eb - in; hash(&d, sizeof d); hash(&n, sizeof n); }
	ntot++;
	key = cid * 8;
	if ((la != lb && !(la != la && lb != lb)) || signbit(la) != signbit(lb) || ea != eb) {
		char x[128], y[128]; snprintf(x, 128, "%La +%d", la, (int)(ea - in)); snprintf(y, 128, "%La +%d", lb, (int)(eb - in));
		report("strtold", in, x, y);
	}
	a = strtod_l(in, &ea, l); b = osv_strtod_l(in, &eb, l);
	cid++;
	ntot++;
	key = cid * 8;
	if (memcmp(&a, &b, sizeof a) || ea != eb) report("strtod_l", in, "", "");
	fa = strtof_l(in, &ea, l); fb = osv_strtof_l(in, &eb, l);
	cid++;
	ntot++;
	key = cid * 8;
	if (memcmp(&fa, &fb, sizeof fa) || ea != eb) report("strtof_l", in, "", "");
	la = strtold_l(in, &ea, l); lb = osv_strtold_l(in, &eb, l);
	cid++;
	ntot++;
	key = cid * 8;
	if ((la != lb && !(la != la && lb != lb)) || signbit(la) != signbit(lb) || ea != eb) report("strtold_l", in, "", "");
}

/* inputs: in each template ',' is the locale's separator, '.' its decimal
 * point, '_' a literal ',', ':' a literal '.', '~' the first byte of the
 * separator alone and '^' the first byte of the decimal point alone */
static void expand(char *out, const char *t, const struct lconv *lc)
{
	*out = 0;
	for (; *t; t++) {
		char one[2] = { *t, 0 };
		if (*t == ',') strcat(out, lc->thousands_sep);
		else if (*t == '.') strcat(out, lc->decimal_point);
		else if (*t == '_') strcat(out, ",");
		else if (*t == ':') strcat(out, ".");
		else if (*t == '~') { one[0] = lc->thousands_sep[0]; strcat(out, one); }
		else if (*t == '^') { one[0] = lc->decimal_point[0]; strcat(out, one); }
		else strcat(out, one);
	}
}

static void test_scan(locale_t l)
{
	static const char *t[] = { "", "1", "1.5", "1_5", "1:5", ".5", "_5", ":5", "-.5", "+1.5", " 1.5", "\t\n 1.5e3",
		"1.", "1.e2", "1.5e", "1.5e+", "1.5e-3x", "1e5.5", "0x1.8p1", "0x1:8p1", "0x1_8p1", "0x.8", "0x", "0xg", "0x1p",
		"0x1.p1", "0x.p1", "inf", "-infinity", "infin", "nan", "nan(12)", "nan(", "-", "+", ".", "_", ":", "e5",
		"1,234", "1,234.5", "1,234,567.25", "12,34", "1,2345", "1,000", "1,00", "1,23,4", "123,456,789", "1234,567",
		"12,345,67", "1,234,5678", "1,234567", "1,234.5,6", ",123", ",5", "1,", "1,.", "1,,234", "0,000", "00,000",
		"000,000", "1,234e3", "1,234,e3", "1,23e3", "-1,234", "+1,234", "1,234,567,890,123", "12,34,567", "1,23,45,678",
		"123,45,678", "12,345", "1,2,3", "1234", "12345", "0x1,234", "1,234_5", "1,234:5", "1.5,3", "99,999.99",
		"12345,678", "123456,789", "1,2345,678", "12,345,6789", "1234,56", "1234,5678", "12345,6", "1,234,56",
		"1234,567,890", "12,3456,789", "1234,567.5", "1,2.5", "12,34.5", "1234,567e2", "1,23e2", "1234,", "1234,.5",
		"99,9999,999", "1,22,333", "333,1,22", "1~", "1~X", "12~34", "1^", "1^5", "^5", "12^X", "123,456", "1~,",
		"0.000,5", "1.000.000", "1e99999", "1e-99999", "0x1p99999", "12345678901234567890", "-99999999999999999999",
		"4294967296", "-2147483649", "1.7976931348623157e308", "4.9e-324", "2.2250738585072011e-308",
		"0.1000000000000000055511151231257827", "1234567890123456789012345678901234567890.5",
		"1,234,567,890,123,456,789,012", "007", "0,007", "08", "0x1A", "1e", "1E+05", ".e1", "x" };
	struct lconv *lc = localeconv();
	char in[512];
	for (unsigned k = 0; k < sizeof t / sizeof *t; k++) {
		expand(in, t[k], lc);
		tmpl = k;
		scan_one(in);
		strto_one(in, l);
	}
}

int main(int argc, char **argv)
{
	setvbuf(stdout, 0, _IOLBF, 0);
	make_flagsets();
	if (getenv("NDIFF_SHOW")) maxshow = atol(getenv("NDIFF_SHOW"));
	if (getenv("NDIFF_CLIST")) clist = fopen(getenv("NDIFF_CLIST"), "w");
	showcls = getenv("NDIFF_SHOWCLASS") != 0;
	for (int i = 1; i < argc; i++) {
		long d0 = ndiff, t0 = ntot, h0 = nhex, p0 = npre, g0 = nglibci, c0 = nc, m0 = npartial, o0 = noverflow;
		cur = argv[i];
		cid = 0;
		osvhash = 1469598103934665603ULL;
		in_c = !strcmp(argv[i], "C") || !strcmp(argv[i], "POSIX") || !strcmp(argv[i], "C.UTF-8");
		if (!setlocale(LC_ALL, argv[i])) {
			printf("SKIP %s: setlocale failed\n", argv[i]);
			ndiff++;
			continue;
		}
		locale_t l = newlocale(LC_ALL_MASK, argv[i], 0);
		test_printf();
		test_scan(l);
		/* the _l variants with a locale other than the current one */
		setlocale(LC_ALL, "C");
		test_scan(l);
		freelocale(l);
		printf("LOCALE %s: %ld differ of %ld (C-DIFF %ld, PRE %ld, MUSL-PARTIAL %ld, MUSL-OVERFLOW %ld, KNOWN-HEX %ld, "
		       "GLIBC-I %ld) OSVHASH %016llx\n", argv[i], ndiff - d0, ntot - t0, nc - c0, npre - p0, npartial - m0,
		       noverflow - o0, nhex - h0, nglibci - g0, (unsigned long long)osvhash);
	}
	printf("SUMMARY %ld differ of %ld (C-DIFF %ld, PRE %ld, MUSL-PARTIAL %ld, MUSL-OVERFLOW %ld, KNOWN-HEX %ld, "
	       "GLIBC-I %ld)\n", ndiff, ntot, nc, npre, npartial, noverflow, nhex, nglibci);
	if (clist) fclose(clist);
	return ndiff != 0;
}
