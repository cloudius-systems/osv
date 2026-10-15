/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Named locales from glibc-format locale data (conf_locale=1). The formats
// and the search order are described at the top of locale-data.cc. The
// codeset test below is also used by locale.cc at conf_locale=0.

#ifndef OSV_LOCALE_DATA_H
#define OSV_LOCALE_DATA_H

#include <stddef.h>
#include <stdint.h>

#define OSV_LOCALE_HIDDEN __attribute__((visibility("hidden")))

// One validated category of a named locale: a category file, or its slice of
// the locale archive. Mapped read-only and kept for the life of the process.
struct osv_locale_data {
    const char *base;
    uint32_t size;
    uint32_t nstrings;
    int cat;
};

// Find, map and validate category cat (glibc numbering, LC_ALL excluded) of
// the locale name. Only UTF-8 locales load. On failure return nullptr with
// errno ENOENT (or EINVAL for a bad category or name, ENOMEM).
OSV_LOCALE_HIDDEN const osv_locale_data *osv_locale_load(int cat, const char *name) noexcept;

// nl_langinfo() of item idx of a loaded category: a pointer into the data,
// or for a word item its value in the pointer bits; "" past nstrings.
OSV_LOCALE_HIDDEN char *osv_locale_item(const osv_locale_data *d, int idx) noexcept;

// String i of item idx read as a list of NUL-terminated strings (ERA,
// ALT_DIGITS), or nullptr if it would not end within the data.
OSV_LOCALE_HIDDEN const char *osv_locale_list(const osv_locale_data *d, int idx,
                                              unsigned i) noexcept;

// The checks osv_locale_load() applies, also used by the fuzz harness
// (scripts/locale-oracle/fuzz-locale-data.cc). osv_locale_check() validates
// one category of size bytes at base; osv_locale_archive_find() finds a
// locale's category in an archive image and validates it. Both return true
// and fill *out on success.
OSV_LOCALE_HIDDEN bool osv_locale_check(int cat, const char *base, size_t size,
                                        osv_locale_data *out) noexcept;
OSV_LOCALE_HIDDEN bool osv_locale_archive_find(const char *archive, size_t size,
                                               const char *name, int cat,
                                               osv_locale_data *out) noexcept;

// Does a codeset name (len bytes) name UTF-8, as glibc decides it? glibc
// accepts it when two spellings agree: the codeset reduced to its letters
// and digits is "utf8" (case-insensitive), and the codeset with only
// letters, digits and "_-.,:" kept, upper-cased, is "UTF8" or "UTF-8". So
// "UTF-8", "utf8" and "UTF+8" match, and "U-T-F-8" and "utf_8" do not.
// (locale.cc for built-in names at any conf_locale, locale-data.cc for
// loaded ones.)
static inline bool osv_codeset_is_utf8(const char *cs, size_t len) noexcept
{
    auto alnum = [](char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
    };
    auto upper = [](char ch) { return ch >= 'a' && ch <= 'z' ? char(ch - 'a' + 'A') : ch; };
    char norm[5], kept[6];
    size_t n = 0, k = 0;
    for (size_t i = 0; i < len; i++) {
        char ch = cs[i];
        if (alnum(ch)) {
            if (n == sizeof(norm) - 1) {
                return false;
            }
            norm[n++] = upper(ch);
        }
        if (alnum(ch) || ch == '_' || ch == '-' || ch == '.' || ch == ',' || ch == ':') {
            if (k == sizeof(kept) - 1) {
                return false;
            }
            kept[k++] = upper(ch);
        }
    }
    norm[n] = '\0';
    kept[k] = '\0';
    return !__builtin_strcmp(norm, "UTF8") &&
        (!__builtin_strcmp(kept, "UTF8") || !__builtin_strcmp(kept, "UTF-8"));
}

#endif
