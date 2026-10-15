/* Copyright (C) 2026 Greg Burd. This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 * Linux stand-ins for the two OSv locale hooks gettext.cc uses (libc/locale/locale.cc), over
 * glibc's locale state. */
#define _GNU_SOURCE 1
#include <langinfo.h>
#include <locale.h>
#include <stdbool.h>
#include <string.h>

static locale_t cur(locale_t l)
{
    return l && l != LC_GLOBAL_LOCALE ? l : uselocale((locale_t)0);
}

const char *__osv_locale_name(int cat, locale_t l)
{
    locale_t c = cur(l);
    if (c == LC_GLOBAL_LOCALE) return setlocale(cat, NULL);
    return nl_langinfo_l(NL_LOCALE_NAME(cat), c);
}

bool __osv_ctype_utf8(locale_t l)
{
    locale_t c = cur(l);
    const char *cs = c == LC_GLOBAL_LOCALE ? nl_langinfo(CODESET) : nl_langinfo_l(CODESET, c);
    return !strcmp(cs, "UTF-8");
}
