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
 * Modified for OSv from musl 1.2.1 src/internal/intscan.c: scanf's '
 * flag accepts the locale's thousands separator in decimal numbers
 * (__osv_intscan; the rules are in osv-scan.h), as glibc 2.41 does.
 */
#include <limits.h>
#include <errno.h>
#include <ctype.h>
#include "shgetc.h"
#include "intscan.h"
#include "floatscan.h"
#include "osv-scan.h"

/* Lookup table for digit values. -1==255>=36 -> invalid */
static const unsigned char table[] = { -1,
-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,-1,-1,-1,-1,-1,-1,
-1,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,
25,26,27,28,29,30,31,32,33,34,35,-1,-1,-1,-1,-1,
-1,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,
25,26,27,28,29,30,31,32,33,34,35,-1,-1,-1,-1,-1,
-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
};

unsigned long long __osv_intscan(FILE *f, unsigned base, int pok, unsigned long long lim,
	const struct osv_num *nmp, int grouped, int *gotdig)
{
	const unsigned char *val = table+1;
	int c, neg=0;
	unsigned x;
	unsigned long long y;
	if (base > 36 || base == 1) {
		errno = EINVAL;
		return 0;
	}
	while (isspace((c=shgetc(f))));
	if (c=='+' || c=='-') {
		neg = -(c=='-');
		c = shgetc(f);
	}
	if ((base == 0 || base == 16) && c=='0') {
		c = shgetc(f);
		if ((c|32)=='x') {
			c = shgetc(f);
			if (val[c]>=16) {
				shunget(f);
				if (pok) shunget(f);
				else shlim(f, 0);
				return 0;
			}
			base = 16;
		} else if (base == 0) {
			base = 8;
		}
	} else {
		if (base == 0) base = 10;
		if (val[c] >= base) {
			shunget(f);
			shlim(f, 0);
			errno = EINVAL;
			return 0;
		}
	}
	if (base == 10 && grouped) {
		struct osv_num nm;
		struct osv_grp gs;
		int check, ovf = 0, ovf_b = 0;
		unsigned long long y_b = 0;
		if (nmp) nm = *nmp;
		else __osv_num(&nm, 0, 0);
		if (!__osv_grouping(&nm)) goto plain;
		/* glibc checks the groups only for a one-byte separator here */
		check = grouped == OSV_GRP_WIDE || !nm.ts[1];
		__osv_grp_init(&gs, &nm, 0);
		for (y=0;; c=shgetc(f)) {
			if (c-'0'<10U) {
				int cut = check && __osv_grp_digit(&gs);
				if (gotdig) *gotdig = 1;
				if (!ovf && (y > ULLONG_MAX/10 || 10*y > ULLONG_MAX-(c-'0'))) ovf = 1;
				else if (!ovf) y = y*10 + (c-'0');
				if (cut) y_b = y, ovf_b = ovf;
			} else if (__osv_match_int(f, &c, nm.ts) == 2) {
				if (check && __osv_grp_gotsep(&gs)) y_b = y, ovf_b = ovf;
			} else break;
		}
		if (c >= 0) shunget(f);	/* (EOF: a partial match gave its bytes back) */
		/* not grouped as the locale says: the longest valid prefix */
		if (check && !__osv_grp_full(&gs)) y = y_b, ovf = ovf_b;
		if (ovf) {
			errno = ERANGE;
			y = lim;
			if (lim&1) neg = 0;
		}
		goto ungot;
	}
plain:
	if (base == 10) {
		for (x=0; c-'0'<10U && x<=UINT_MAX/10-1; c=shgetc(f))
			x = x*10 + (c-'0');
		for (y=x; c-'0'<10U && y<=ULLONG_MAX/10 && 10*y<=ULLONG_MAX-(c-'0'); c=shgetc(f))
			y = y*10 + (c-'0');
		if (c-'0'>=10U) goto done;
	} else if (!(base & base-1)) {
		int bs = "\0\1\2\4\7\3\6\5"[(0x17*base)>>5&7];
		for (x=0; val[c]<base && x<=UINT_MAX/32; c=shgetc(f))
			x = x<<bs | val[c];
		for (y=x; val[c]<base && y<=ULLONG_MAX>>bs; c=shgetc(f))
			y = y<<bs | val[c];
	} else {
		for (x=0; val[c]<base && x<=UINT_MAX/36-1; c=shgetc(f))
			x = x*base + val[c];
		for (y=x; val[c]<base && y<=ULLONG_MAX/base && base*y<=ULLONG_MAX-val[c]; c=shgetc(f))
			y = y*base + val[c];
	}
	if (val[c]<base) {
		for (; val[c]<base; c=shgetc(f));
		errno = ERANGE;
		y = lim;
		if (lim&1) neg = 0;
	}
done:
	shunget(f);
ungot:
	if (y>=lim) {
		if (!(lim&1) && !neg) {
			errno = ERANGE;
			return lim-1;
		} else if (y>lim) {
			errno = ERANGE;
			return lim;
		}
	}
	return (y^neg)-neg;
}

unsigned long long __intscan(FILE *f, unsigned base, int pok, unsigned long long lim)
{
	return __osv_intscan(f, base, pok, lim, 0, 0, 0);
}
