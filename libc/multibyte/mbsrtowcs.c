/* 
 * This code was written by Rich Felker in 2010; no copyright is claimed.
 * This code is in the public domain. Attribution is appreciated but
 * unnecessary.
 */

#include <stdlib.h>
#include <inttypes.h>
#include <wchar.h>
#include <errno.h>

#include "internal.h"

size_t mbsrtowcs(wchar_t *restrict ws, const char **restrict src, size_t wn, mbstate_t *restrict st)
{
	unsigned c;
	const unsigned char *s = (const void *)*src;
	const wchar_t *wsorig = ws;

	if (!st) st = (void *)&c, c = 0;
	else c = *(unsigned *)st;

	if (c) {
		*(unsigned *)st = 0;
		if (!ws) {
			wn = 0;
			goto resume0;
		}
		goto resume;
	}

	if (!ws) for (wn=0;;) {
		if (*s-SA >= SB-SA) {
			while (((uintptr_t)s&3) && *s-1u<0x7f) s++, wn++;
			while (!(( *(uint32_t*)s | *(uint32_t*)s-0x01010101) & 0x80808080)) s+=4, wn+=4;
			while (*s-1u<0x7f) s++, wn++;
			if (!*s) return wn;
			if (*s-SA >= SB-SA) goto ilseq2;
		}
		c = bittab[*s++-SA];
		do {
resume0:
			if (OOB(c,*s)) goto ilseq2; s++;
			c <<= 6; if (!(c&(1U<<31))) break;
			if (*s++-0x80u >= 0x40) goto ilseq2;
			c <<= 6; if (!(c&(1U<<31))) break;
			if (*s++-0x80u >= 0x40) goto ilseq2;
		} while (0);
		wn++; c = 0;
	}

	while (wn) {
		if (*s-SA >= SB-SA) {
			if (wn >= 7) {
				while (((uintptr_t)s&3) && *s-1u<0x7f) {
					*ws++ = *s++;
					wn--;
				}
				while (wn>=4 && !(( *(uint32_t*)s | *(uint32_t*)s-0x01010101) & 0x80808080)) {
					*ws++ = *s++;
					*ws++ = *s++;
					*ws++ = *s++;
					*ws++ = *s++;
					wn -= 4;
				}
			}
			while (wn && *s-1u<0x7f) {
				*ws++ = *s++;
				wn--;
			}
			if (!wn) break;
			if (!*s) {
				*ws = 0;
				*src = 0;
				return ws-wsorig;
			}
			if (*s-SA >= SB-SA) goto ilseq;
		}
		c = bittab[*s++-SA];
		do {
resume:
			if (OOB(c,*s)) goto ilseq;
			c = (c<<6) | *s++-0x80;
			if (!(c&(1U<<31))) break;

			if (*s-0x80u >= 0x40) goto ilseq;
			c = (c<<6) | *s++-0x80;
			if (!(c&(1U<<31))) break;

			if (*s-0x80u >= 0x40) goto ilseq;
			c = (c<<6) | *s++-0x80;
		} while (0);

		*ws++ = c; wn--; c = 0;
	}
	*src = (const void *)s;
	return ws-wsorig;
ilseq:
	*src = (const void *)s;
ilseq2:
	/* enter permanently failing state */
	*(unsigned *)st = FAILSTATE;
	errno = EILSEQ;
	return -1;
}
