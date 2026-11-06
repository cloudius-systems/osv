/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

/* __osv_num(): the LC_NUMERIC items (and LC_CTYPE's outdigits for the I
 * flag) of a locale, for printf, scanf and strtod (conf_locale=1 only). */

#include <langinfo.h>
#include <locale.h>
#include <string.h>
#include <wchar.h>
#include "osv-numeric.h"

/* OSv's takes a null locale as the current one (libc/locale/locale.cc). */
char *__nl_langinfo_l(nl_item, locale_t);

#define NUM_ITEM(i) ((LC_NUMERIC << 16) | (i))
#define OUTDIGIT_MB(d) ((LC_CTYPE << 16) | (41 + (d)))

void __osv_num(struct osv_num *n, locale_t l, int digits)
{
	int d;
	const char *s;
	n->dp = __nl_langinfo_l(NUM_ITEM(0), l);
	n->ts = __nl_langinfo_l(NUM_ITEM(1), l);
	n->grp = __nl_langinfo_l(NUM_ITEM(2), l);
	if (!*n->dp) n->dp = ".";
	s = n->dp;
	n->dpw = __osv_u8dec(&s);
	s = n->ts;
	n->tsw = *s ? __osv_u8dec(&s) : 0;
	n->dig[0] = 0;
	if (!digits) return;
	/* A built-in LC_CTYPE (C, C.UTF-8) has no outdigits: ASCII. */
	for (d = 0; d < 10; d++) {
		n->dig[d] = __nl_langinfo_l(OUTDIGIT_MB(d), l);
		if (!*n->dig[d]) {
			n->dig[0] = 0;
			return;
		}
	}
}
