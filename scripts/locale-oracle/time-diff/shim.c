/* Copyright (C) 2026 Greg Burd. This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 * Linux stand-in for OSv's __osv_nl_list (libc/locale/locale.cc), over glibc's nl_langinfo_l. */
#define _GNU_SOURCE 1
#include <langinfo.h>
#include <locale.h>
#include <string.h>
const char *__osv_nl_list(nl_item item, unsigned i, locale_t l)
{
    const char *s = (!l || l == LC_GLOBAL_LOCALE) ? nl_langinfo(item) : nl_langinfo_l(item, l);
    if (!*s && i) return 0;
    for (; i; i--) s += strlen(s) + 1;
    return s;
}
locale_t __osv_c_locale(void)
{
    static locale_t c;
    if (!c) c = newlocale(LC_ALL_MASK, "C", 0);
    return c;
}
char *osv_shim_nl_langinfo_l(nl_item it, locale_t l)
{
    if (!l || l == LC_GLOBAL_LOCALE) return nl_langinfo(it);
    return nl_langinfo_l(it, l);
}
