#include "stdio_impl.h"
#include "shgetc.h"
#include "intscan.h"
#include "floatscan.h"

#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <wchar.h>
#include <wctype.h>
#include <limits.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <float.h>
#include <inttypes.h>

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

static int readwc(int c, wchar_t **wcs, mbstate_t *st)
{
	char ch = c;
	wchar_t wc;
	switch (mbrtowc(&wc, &ch, 1, st)) {
	case -1:
		return -1;
	case -2:
		break;
	default:
		if (*wcs) *(*wcs)++ = wc;
	}
	return 0;
}

int vfscanf(FILE *restrict f, const char *restrict fmt, va_list ap)
{
	int width;
	int size;
	int base;
	const unsigned char *p;
	int c, t;
	char *s;
	wchar_t *wcs;
	mbstate_t st;
	void *dest=NULL;
	int invert;
	int matches=0;
	unsigned long long x;
#ifdef HAVE_FLOAT_SUPPORT
	long double y;
#endif
	off_t pos = 0;

	FLOCK(f);

	for (p=(const unsigned char *)fmt; *p; p++) {

		if (isspace(*p)) {
			while (isspace(p[1])) p++;
			shlim(f, 0);
			while (isspace(shgetc(f)));
			shunget(f);
			pos += shcnt(f);
			continue;
		}
		if (*p != '%' || p[1] == '%') {
			p += *p=='%';
			shlim(f, 0);
			c = shgetc(f);
			if (c!=*p) {
				shunget(f);
				if (c<0) goto input_fail;
				goto match_fail;
			}
			pos++;
			continue;
		}

		p++;
		if (*p=='*') {
			dest = 0; p++;
		} else if (isdigit(*p) && p[1]=='$') {
			dest = arg_n(ap, *p-'0'); p+=2;
		} else {
			dest = va_arg(ap, void *);
		}

		for (width=0; isdigit(*p); p++) {
			width = 10*width + *p - '0';
		}

		if (*p=='m') {
			p++;
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

		switch (t) {
		case 'C':
		case 'c':
			if (width < 1) width = 1;
		case 's':
			if (size == SIZE_l) t &= ~0x20;
		case 'd': case 'i': case 'o': case 'u': case 'x':
		case 'a': case 'e': case 'f': case 'g':
		case 'A': case 'E': case 'F': case 'G': case 'X':
		case '[': case 'S':
		case 'p': case 'n':
			if (width < 1) width = 0;
			break;
		default:
			goto fmt_fail;
		}

		shlim(f, width);

		if (t != 'n') {
			if (shgetc(f) < 0) goto input_fail;
			shunget(f);
		}

		switch (t) {
		case 'n':
			store_int(dest, size, pos);
			/* do not increment match count, etc! */
			continue;
		case 'C':
			wcs = dest;
			st = (mbstate_t){ 0 };
			while ((c=shgetc(f)) >= 0) {
				if (readwc(c, &wcs, &st) < 0)
					goto input_fail;
			}
			if (!mbsinit(&st)) goto input_fail;
			if (shcnt(f) != width) goto match_fail;
			break;
		case 'c':
			if (dest) {
				s = dest;
				while ((c=shgetc(f)) >= 0) *s++ = c;
			} else {
				while (shgetc(f)>=0);
			}
			if (shcnt(f) < width) goto match_fail;
			break;
		case '[':
			s = dest;
			wcs = dest;

			if (*++p == '^') p++, invert = 1;
			else invert = 0;

			unsigned char scanset[257];
			memset(scanset, invert, sizeof scanset);

			scanset[0] = 0;
			if (*p == '-') p++, scanset[1+'-'] = 1-invert;
			else if (*p == ']') p++, scanset[1+']'] = 1-invert;
			for (; *p != ']'; p++) {
				if (!*p) goto fmt_fail;
				if (*p=='-' && p[1] && p[1] != ']')
					for (c=p++[-1]; c<*p; c++)
						scanset[1+c] = 1-invert;
				scanset[1+*p] = 1-invert;
			}

			if (size == SIZE_l) {
				st = (mbstate_t){0};
				while (scanset[(c=shgetc(f))+1]) {
					if (readwc(c, &wcs, &st) < 0)
						goto input_fail;
				}
				if (!mbsinit(&st)) goto input_fail;
				s = 0;
			} else if (s) {
				while (scanset[(c=shgetc(f))+1])
					*s++ = c;
				wcs = 0;
			} else {
				while (scanset[(c=shgetc(f))+1]);
			}
			shunget(f);
			if (!shcnt(f)) goto match_fail;
			if (s) *s = 0;
			if (wcs) *wcs = 0;
			break;
		default:
			shlim(f, 0);
			while (isspace(shgetc(f)));
			shunget(f);
			pos += shcnt(f);
			shlim(f, width);
			if (shgetc(f) < 0) goto input_fail;
			shunget(f);
		}

		switch (t) {
		case 'p':
		case 'X':
		case 'x':
			base = 16;
			goto int_common;
		case 'o':
			base = 8;
			goto int_common;
		case 'd':
		case 'u':
			base = 10;
			goto int_common;
		case 'i':
			base = 0;
		int_common:
			x = __intscan(f, base, 0, ULLONG_MAX);
			if (!shcnt(f)) goto match_fail;
			if (t=='p' && dest) *(void **)dest = (void *)(uintptr_t)x;
			else store_int(dest, size, x);
			break;
#ifdef HAVE_FLOAT_SUPPORT
		case 'a': case 'A':
		case 'e': case 'E':
		case 'f': case 'F':
		case 'g': case 'G':
			y = __floatscan(f, size, 0);
			if (!shcnt(f)) goto match_fail;
			if (dest) switch (size) {
			case SIZE_def:
				*(float *)dest = y;
				break;
			case SIZE_l:
				*(double *)dest = y;
				break;
			case SIZE_L:
				*(long double *)dest = y;
				break;
			}
			break;
#endif
		case 'S':
			wcs = dest;
			st = (mbstate_t){ 0 };
			while (!isspace(c=shgetc(f)) && c!=EOF) {
				if (readwc(c, &wcs, &st) < 0)
					goto input_fail;
			}
			shunget(f);
			if (!mbsinit(&st)) goto input_fail;
			if (dest) *wcs++ = 0;
			break;
		case 's':
			if (dest) {
				s = dest;
				while (!isspace(c=shgetc(f)) && c!=EOF)
					*s++ = c;
				*s = 0;
			} else {
				while (!isspace(c=shgetc(f)) && c!=EOF);
			}
			shunget(f);
			break;
		}

		pos += shcnt(f);
		if (dest) matches++;
	}
	if (0) {
fmt_fail:
input_fail:
		if (!matches) matches--;
	}
match_fail:
	FUNLOCK(f);
	return matches;
}
