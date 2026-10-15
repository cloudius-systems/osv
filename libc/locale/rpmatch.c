/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

/* rpmatch(): 1 if the response matches the LC_MESSAGES YESEXPR of the
 * current locale, 0 if it matches NOEXPR, -1 if neither (or a regex fails
 * to compile), as glibc 2.41 behaves (measured). Both are POSIX extended
 * regular expressions; each call compiles them for the locale current at
 * the call, so a change of locale takes effect at once.
 * ponytail: compiled on every call; a per-locale cache if a caller ever
 * shows this in a profile. */

#include <langinfo.h>
#include <regex.h>
#include <stdlib.h>

static int match(nl_item item, const char *s)
{
	regex_t re;
	if (regcomp(&re, nl_langinfo(item), REG_EXTENDED | REG_NOSUB)) return -1;
	int r = regexec(&re, s, 0, 0, 0);
	regfree(&re);
	return r == 0 ? 1 : r == REG_NOMATCH ? 0 : -1;
}

int rpmatch(const char *response)
{
	int r = match(YESEXPR, response);
	if (r) return r;
	r = match(NOEXPR, response);
	return r > 0 ? 0 : -1;
}
