/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Internal interface of gettext (gettext.cc) for libc's own messages
// (messages.cc), and the two locale hooks gettext.cc uses (locale.cc).

#ifndef OSV_INTL_H
#define OSV_INTL_H

#include <locale.h>

#ifdef __cplusplus
#define OSV_INTL_NOEXCEPT noexcept
extern "C" {
#else
#define OSV_INTL_NOEXCEPT
#endif

#define OSV_INTL_HIDDEN __attribute__((visibility("hidden")))

// msgid1 (msgid2: the plural form for n, or null) translated in domain
// (null: the text domain) through category cat of locale l (null: the
// thread's), with glibc's rules; msgid1 or msgid2 if there is none. errno is
// preserved.
OSV_INTL_HIDDEN const char *__osv_dcngettext_l(const char *domain, const char *msgid1,
                                               const char *msgid2, unsigned long n,
                                               int cat, locale_t l) OSV_INTL_NOEXCEPT;

// The name of category cat of locale l (null: the thread's locale).
OSV_INTL_HIDDEN const char *__osv_locale_name(int cat, locale_t l) OSV_INTL_NOEXCEPT;

// Is LC_CTYPE of l (null: the thread's locale) UTF-8?
#ifdef __cplusplus
OSV_INTL_HIDDEN bool __osv_ctype_utf8(locale_t l) noexcept;
#else
OSV_INTL_HIDDEN _Bool __osv_ctype_utf8(locale_t l);
#endif

#ifdef __cplusplus
}
#endif

#endif
