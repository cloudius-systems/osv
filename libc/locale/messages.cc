/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// libc's own messages: strerror, strerror_l, strsignal, gai_strerror,
// regerror (and through strerror perror, error and printf's %m), with glibc
// 2.41's English text (messages-en.inc, generated from its output), which is
// also the msgid of glibc's "libc" message catalog. With conf_locale=1 each
// is translated through that domain as glibc does (gettext.cc: LANGUAGE,
// LC_MESSAGES, the libc.mo of Debian's locales package under
// /usr/share/locale, or wherever bindtextdomain("libc", ...) points).
// Measured on glibc (scripts/locale-oracle/messages/lmp.c, rules5.sh,
// rules12.sh):
// - an unknown errno is translate("Unknown error ") followed by the number;
//   an unknown signal translate("Unknown signal %d"), a real-time one
//   translate("Real-time signal %d") with n - SIGRTMIN (OSv's SIGRTMIN is
//   35, glibc's 34: the text follows each system's own numbering);
// - gai_strerror's unknown "Unknown error" is not translated;
// - strerror_l uses its locale's LC_MESSAGES and LC_CTYPE, LANGUAGE too;
// - strerrordesc_np is never translated and is null for an unknown errno.
// A translated "%d" format is filled in here without printf: only its first
// "%d" is replaced, so a catalog cannot pass other conversions to printf.

#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <netdb.h>
#include <regex.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

#ifdef __OSV__
#include <osv/export.h>
#else
#define OSV_LIBC_API
#endif

#if OSV_CONF_LOCALE
#include "intl.h"
#endif

namespace {

#include "messages-en.inc"

const char *tr(const char *msg, locale_t l)
{
#if OSV_CONF_LOCALE
    return __osv_dcngettext_l("libc", msg, nullptr, 1, LC_MESSAGES, l);
#else
    (void)l;
    return msg;
#endif
}

// fmt with its first "%d" replaced by n, into buf.
char *fill(char *buf, size_t size, const char *fmt, long n)
{
    char num[24];
    snprintf(num, sizeof(num), "%ld", n);
    const char *d = strstr(fmt, "%d");
    size_t pre = d ? size_t(d - fmt) : strlen(fmt);
    snprintf(buf, size, "%.*s%s%s", int(pre), fmt, d ? num : "", d ? d + 2 : "");
    return buf;
}

// The longest translation of these messages in Debian's libc.mo files is
// 71 bytes; a longer one is cut.
constexpr size_t unknown_max = 128;
__thread char strerror_buf[unknown_max];
__thread char strsignal_buf[unknown_max];

const char *en_error(int e)
{
    return e >= 0 && size_t(e) < sizeof(en_errors) / sizeof(*en_errors) ? en_errors[e] : nullptr;
}

} // namespace

extern "C" OSV_LIBC_API
char *__strerror_l(int e, locale_t l) noexcept
{
    int saved = errno;
    const char *s = en_error(e);
    char *r;
    if (s) {
        r = const_cast<char *>(tr(s, l));
    } else {
        // "Unknown error " is the msgid; the number follows it as is.
        snprintf(strerror_buf, sizeof(strerror_buf), "%s%d", tr("Unknown error ", l), e);
        r = strerror_buf;
    }
    errno = saved;
    return r;
}

// Weak, as musl's was (no change to the exported symbol table).
#define OSV_STR2(x) #x
#define OSV_STR(x) OSV_STR2(x)
extern "C" OSV_LIBC_API
char *strerror_l(int e, locale_t l) noexcept __attribute__((weak, alias(OSV_STR(__strerror_l))));

extern "C" OSV_LIBC_API
char *strerror(int e) noexcept
{
    return __strerror_l(e, nullptr);
}

extern "C" OSV_LIBC_API
const char *strerrordesc_np(int e) noexcept
{
    return en_error(e);
}

extern "C" OSV_LIBC_API
char *strsignal(int sig) noexcept
{
    int saved = errno;
    char *r;
    if (sig >= 1 && sig < 32 && en_signals[sig]) {
        r = const_cast<char *>(tr(en_signals[sig], nullptr));
    } else if (sig >= SIGRTMIN && sig <= SIGRTMAX) {
        r = fill(strsignal_buf, sizeof(strsignal_buf), tr("Real-time signal %d", nullptr),
                 sig - SIGRTMIN);
    } else {
        r = fill(strsignal_buf, sizeof(strsignal_buf), tr("Unknown signal %d", nullptr), sig);
    }
    errno = saved;
    return r;
}

extern "C" OSV_LIBC_API
const char *gai_strerror(int code) noexcept
{
    int saved = errno;
    const char *r = "Unknown error";
    for (const auto &g : en_gai) {
        if (g.code == code) {
            r = tr(g.msg, nullptr);
            break;
        }
    }
    errno = saved;
    return r;
}

extern "C" OSV_LIBC_API
size_t regerror(int e, const regex_t *, char *buf, size_t size)
{
    int saved = errno;
    // glibc aborts on a code past REG_ERPAREN; OSv says so.
    const char *s = e >= 0 && size_t(e) < sizeof(en_regerror) / sizeof(*en_regerror) ?
        tr(en_regerror[e], nullptr) : "Unknown error";
    size_t len = strlen(s) + 1;
    if (size) {
        size_t n = len < size ? len : size;
        memcpy(buf, s, n - 1);
        buf[n - 1] = '\0';
    }
    errno = saved;
    return len;
}
