/*
 * Copyright © 2005-2020 Rich Felker, et al.
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
 *
 * Modified for OSv from musl 1.2.1 src/stdio/vfwscanf.c: numbers are read
 * by the narrow scanners through a one-byte-per-character view of the
 * stream (numview), in which the locale's decimal point and thousands
 * separator are single bytes, so floats take the locale's decimal point
 * and the ' flag its separator in d, i, u and floats, as glibc 2.41's
 * vfwscanf does (measured; the rules are in libc/internal/osv-scan.h).
 * With conf_locale=0 the locale is always C, where ' changes nothing.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <wchar.h>
#include <wctype.h>
#include <limits.h>
#include <string.h>

#include "stdio_impl.h"
#include "shgetc.h"
#include "intscan.h"
#include "floatscan.h"
#include "osv-numeric.h"
#include <stdint.h>

#define SIZE_hh -2
#define SIZE_h  -1
#define SIZE_def 0
#define SIZE_l   1
#define SIZE_L   2
#define SIZE_ll  3

static void store_int(void *dest, int size, unsigned long long i)
{
	if (!dest) return;
	switch (size) {
	case SIZE_hh:
		*(char *)dest = i;
		break;
	case SIZE_h:
		*(short *)dest = i;
		break;
	case SIZE_def:
		*(int *)dest = i;
		break;
	case SIZE_l:
		*(long *)dest = i;
		break;
	case SIZE_ll:
		*(long long *)dest = i;
		break;
	}
}

static void *arg_n(va_list ap, unsigned int n)
{
	void *p;
	unsigned int i;
	va_list ap2;
	va_copy(ap2, ap);
	for (i=n; i>1; i--) va_arg(ap2, void *);
	p = va_arg(ap2, void *);
	va_end(ap2);
	return p;
}

static int in_set(const wchar_t *set, int c)
{
	int j;
	const wchar_t *p = set;
	if (*p == '-') {
		if (c=='-') return 1;
		p++;
	} else if (*p == ']') {
		if (c==']') return 1;
		p++;
	}
	for (; *p && *p != ']'; p++) {
		if (*p=='-' && p[1] && p[1] != ']')
			for (j=p++[-1]; j<*p; j++)
				if (c==j) return 1;
		if (c==*p) return 1;
	}
	return 0;
}

#if 1
#undef getwc
#define getwc(f) \
	((f)->rpos != (f)->rend && *(f)->rpos < 128 ? *(f)->rpos++ : (getwc)(f))

#undef ungetwc
#define ungetwc(c,f) \
	((f)->rend && (c)<128U ? *--(f)->rpos : ungetwc((c),(f)))
#endif

/* A number is read from the wide stream src through a pseudo FILE that
 * holds one byte per character: ASCII as itself, the locale's decimal point
 * and separator as 0x80 and 0x81 if they are not ASCII (the osv_num for the
 * scanners says so), anything else as 0xff, which no scanner accepts. One
 * character is read at a time, so at the end at most the last one read is
 * not consumed, and it goes back to src. */
struct numview {
	FILE *src;
	const struct osv_num *nm;
	unsigned char b[8];
	wint_t last;
	off_t nread;
};

static size_t numview_read(FILE *g, unsigned char *buf, size_t len)
{
	struct numview *v = g->cookie;
	wint_t c = getwc(v->src);
	unsigned char b;
	if (c == WEOF) return 0;
	v->last = c;
	v->nread++;
	if (c < 128) b = c;
	else if (c == v->nm->dpw) b = 0x80;
	else if (v->nm->tsw && c == v->nm->tsw) b = 0x81;
	else b = 0xff;
	g->buf[0] = b;
	g->rpos = g->buf + 1;
	g->rend = g->buf + 1;
	*buf = b;
	return 1;
}

int vfwscanf(FILE *restrict f, const wchar_t *restrict fmt, va_list ap)
{
	int width;
	int size;
	int alloc;
	const wchar_t *p;
	int c, t;
	char *s;
	wchar_t *wcs;
	void *dest=NULL;
	int invert;
	int matches=0;
	off_t pos = 0, cnt;
	char tmp[3*sizeof(int)+10];
	const wchar_t *set;
	size_t i, k;
	int grouped, base;
	unsigned long long x;
	long double y;
	struct osv_num nm, vnm;
	char vdp[2] = "", vts[2] = "";
	int have_nm = 0;
	struct numview v;
	FILE g;

	FLOCK(f);

	fwide(f, 1);

	for (p=fmt; *p; p++) {

		alloc = 0;

		if (iswspace(*p)) {
			while (iswspace(p[1])) p++;
			while (iswspace((c=getwc(f)))) pos++;
			ungetwc(c, f);
			continue;
		}
		if (*p != '%' || p[1] == '%') {
			if (*p == '%') {
				p++;
				while (iswspace((c=getwc(f)))) pos++;
			} else {
				c = getwc(f);
			}
			if (c!=*p) {
				ungetwc(c, f);
				if (c<0) goto input_fail;
				goto match_fail;
			}
			pos++;
			continue;
		}

		p++;
		grouped = 0;
#if OSV_CONF_LOCALE
		/* glibc's flags: ' (grouping) and I (locale digits: accepted,
		 * but OSv reads ASCII digits only), before or after * and n$ */
		for (; *p=='\'' || *p=='I'; p++) grouped |= *p=='\'';
#endif
		if (*p=='*') {
			dest = 0; p++;
		} else if (iswdigit(*p) && p[1]=='$') {
			dest = arg_n(ap, *p-'0'); p+=2;
		} else {
			dest = va_arg(ap, void *);
		}
#if OSV_CONF_LOCALE
		for (; *p=='\'' || *p=='I'; p++) grouped |= *p=='\'';
#endif

		for (width=0; iswdigit(*p); p++) {
			width = 10*width + *p - '0';
		}

		if (*p=='m') {
			wcs = 0;
			s = 0;
			alloc = !!dest;
			p++;
		} else {
			alloc = 0;
		}

		size = SIZE_def;
		switch (*p++) {
		case 'h':
			if (*p == 'h') p++, size = SIZE_hh;
			else size = SIZE_h;
			break;
		case 'l':
			if (*p == 'l') p++, size = SIZE_ll;
			else size = SIZE_l;
			break;
		case 'j':
			size = SIZE_ll;
			break;
		case 'z':
		case 't':
			size = SIZE_l;
			break;
		case 'L':
			size = SIZE_L;
			break;
		case 'd': case 'i': case 'o': case 'u': case 'x':
		case 'a': case 'e': case 'f': case 'g':
		case 'A': case 'E': case 'F': case 'G': case 'X':
		case 's': case 'c': case '[':
		case 'S': case 'C':
		case 'p': case 'n':
			p--;
			break;
		default:
			goto fmt_fail;
		}

		t = *p;

		/* Transform S,C -> ls,lc */
		if ((t&0x2f)==3) {
			size = SIZE_l;
			t |= 32;
		}

		if (t != 'n') {
			if (t != '[' && (t|32) != 'c')
				while (iswspace((c=getwc(f)))) pos++;
			else
				c=getwc(f);
			if (c < 0) goto input_fail;
			ungetwc(c, f);
		}

		switch (t) {
		case 'n':
			store_int(dest, size, pos);
			/* do not increment match count, etc! */
			continue;

		case 's':
		case 'c':
		case '[':
			if (t == 'c') {
				if (width<1) width = 1;
				invert = 1;
				set = L"";
			} else if (t == 's') {
				invert = 1;
				static const wchar_t spaces[] = {
					' ', '\t', '\n', '\r', 11, 12,  0x0085,
					0x2000, 0x2001, 0x2002, 0x2003, 0x2004, 0x2005,
					0x2006, 0x2008, 0x2009, 0x200a,
					0x2028, 0x2029, 0x205f, 0x3000, 0 };
				set = spaces;
			} else {
				if (*++p == '^') p++, invert = 1;
				else invert = 0;
				set = p;
				if (*p==']') p++;
				while (*p!=']') {
					if (!*p) goto fmt_fail;
					p++;
				}
			}

			s = (size == SIZE_def) ? dest : 0;
			wcs = (size == SIZE_l) ? dest : 0;

			int gotmatch = 0;

			if (width < 1) width = -1;

			i = 0;
			if (alloc) {
				k = t=='c' ? width+1U : 31;
				if (size == SIZE_l) {
					wcs = malloc(k*sizeof(wchar_t));
					if (!wcs) goto alloc_fail;
				} else {
					s = malloc(k);
					if (!s) goto alloc_fail;
				}
			}
			while (width) {
				if ((c=getwc(f))<0) break;
				if (in_set(set, c) == invert)
					break;
				if (wcs) {
					wcs[i++] = c;
					if (alloc && i==k) {
						k += k+1;
						wchar_t *tmp = realloc(wcs, k*sizeof(wchar_t));
						if (!tmp) goto alloc_fail;
						wcs = tmp;
					}
				} else if (size != SIZE_l) {
					int l = wctomb(s?s+i:tmp, c);
					if (l<0) goto input_fail;
					i += l;
					if (alloc && i > k-4) {
						k += k+1;
						char *tmp = realloc(s, k);
						if (!tmp) goto alloc_fail;
						s = tmp;
					}
				}
				pos++;
				width-=(width>0);
				gotmatch=1;
			}
			if (width) {
				ungetwc(c, f);
				if (t == 'c' || !gotmatch) goto match_fail;
			}

			if (alloc) {
				if (size == SIZE_l) *(wchar_t **)dest = wcs;
				else *(char **)dest = s;
			}
			if (t != 'c') {
				if (wcs) wcs[i] = 0;
				if (s) s[i] = 0;
			}
			break;

		case 'd': case 'i': case 'o': case 'u': case 'x':
		case 'a': case 'e': case 'f': case 'g':
		case 'A': case 'E': case 'F': case 'G': case 'X':
		case 'p':
			if (!have_nm) {
				__osv_num(&nm, 0, 0);
				vnm = nm;
				vdp[0] = nm.dpw < 128 ? nm.dpw : 0x80;
				vts[0] = !nm.tsw ? 0 : nm.tsw < 128 ? nm.tsw : 0x81;
				vnm.dp = vdp;
				vnm.ts = vts;
				have_nm = 1;
			}
			v = (struct numview){ .src = f, .nm = &nm };
			/* numview has a 4-byte UNGET margin (b[8], buf=b+4). The
			 * byte scanners are always driven here with pok=0, so the
			 * multi-shunget runs ("infinity"/"nan(seq)") are gated off
			 * and pushback stays within the margin. If wcstod/wcstof/
			 * wcstold are ever routed through numview with pok=1, enlarge
			 * this margin (or use a string buffer) or they read OOB. */
			g = (FILE){ .buf = v.b + 4, .rpos = v.b + 4, .rend = v.b + 4,
				.read = numview_read, .cookie = &v, .no_locking = true };
			shlim(&g, width > 0 ? width : 0);
			switch (t) {
			case 'p': case 'X': case 'x': base = 16; break;
			case 'o': base = 8; break;
			case 'd': case 'u': base = 10; break;
			case 'i': base = 0; break;
			default: base = -1;
			}
			if (base >= 0)
				x = __osv_intscan(&g, base, 0, ULLONG_MAX, &vnm,
					grouped && base <= 10 ? OSV_GRP_WIDE : 0, 0);
			else
				y = __osv_floatscan(&g, size, 0, &vnm, grouped ? OSV_GRP_WIDE : 0);
			cnt = shcnt(&g);
			if (v.nread > cnt) ungetwc(v.last, f);
			if (!cnt) goto match_fail;
			if (base < 0) {
				if (dest) switch (size) {
				case SIZE_def: *(float *)dest = y; break;
				case SIZE_l: *(double *)dest = y; break;
				case SIZE_L: *(long double *)dest = y; break;
				}
			} else if (t=='p' && dest) *(void **)dest = (void *)(uintptr_t)x;
			else store_int(dest, size, x);
			pos += cnt;
			break;
		default:
			goto fmt_fail;
		}

		if (dest) matches++;
	}
	if (0) {
fmt_fail:
alloc_fail:
input_fail:
		if (!matches) matches--;
match_fail:
		if (alloc) {
			free(s);
			free(wcs);
		}
	}
	FUNLOCK(f);
	return matches;
}

weak_alias(vfwscanf,__isoc99_vfwscanf);
