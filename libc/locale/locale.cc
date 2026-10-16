/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// OSv's locale core: setlocale(), newlocale(), duplocale(), freelocale(),
// uselocale() and nl_langinfo_l(), with glibc's observable behaviour.
//
// OSv runs Linux applications built against glibc, so a locale request must
// get the answer glibc would give. OSv provides the two locales of glibc with
// the distribution's standard C.utf8 installed (Debian/Ubuntu libc-bin,
// Fedora glibc):
//
//   "C" (also spelled "POSIX", always reported as "C"), codeset ANSI_X3.4-1968;
//   "C.<codeset>[@modifier]" where the codeset normalises to "utf8" (C.UTF-8,
//   C.utf8, C.UTF8, ...), reported with the spelling it was requested with,
//   codeset UTF-8.
//
// Every other name fails with ENOENT. A name containing '/' or a ".." path
// element, or longer than 255 bytes, fails with EINVAL. A composite name
// ("LC_CTYPE=...;LC_NUMERIC=...;..." naming all twelve categories, as
// setlocale(LC_ALL, NULL) returns it) is accepted by setlocale(LC_ALL) and
// newlocale(). The empty name selects each category from LC_ALL, then
// LC_<category>, then LANG, then "C".
//
// OSv's multibyte conversion is UTF-8 in every locale and MB_CUR_MAX is 4, as
// before; only the names, codesets and nl_langinfo() items follow the locale.
//
// The global locale and the C locale object are never freed. Locale names
// are interned and never freed either, so a returned name stays valid.

#include <errno.h>
#include <langinfo.h>
#include <locale.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <osv/export.h>
#include <osv/mutex.h>

#include "libc/internal/locale_impl.h"
#include "libc/internal/libc.h"

// glibc's category numbering (LC_ALL is 6; LC_PAPER..LC_IDENTIFICATION 7..12).
enum { NCAT = 13 };

#define NL_ITEM(cat, idx) (((cat) << 16) | (idx))

static const char *const category_names[NCAT] = {
    "LC_CTYPE", "LC_NUMERIC", "LC_TIME", "LC_COLLATE", "LC_MONETARY",
    "LC_MESSAGES", "LC_ALL", "LC_PAPER", "LC_NAME", "LC_ADDRESS",
    "LC_TELEPHONE", "LC_MEASUREMENT", "LC_IDENTIFICATION",
};

// glibc's LC_ALL_MASK: every category bit except bit 6 (LC_ALL itself).
static constexpr int all_mask = ((1 << NCAT) - 1) & ~(1 << LC_ALL);

static constexpr size_t name_max = 255;

extern locale_t __c_locale_ptr;

// The locale this thread selected with uselocale(); null means the global
// locale. musl's wide stdio code swaps it temporarily through CURRENT_LOCALE.
OSV_HIDDEN __thread locale_t __current_locale;

namespace {

mutex locale_mutex;

// The process-wide locale that setlocale() changes and that a thread using
// LC_GLOBAL_LOCALE sees. Its ctype tables are the C locale's. Readers do not
// take locale_mutex: names are interned and never freed, and a name pointer
// is updated with one aligned store (glibc's setlocale is likewise MT-Unsafe
// against concurrent use of the global locale).
struct __locale_struct &global_locale();

// Interned locale names: a linear list under locale_mutex; a
// program uses a handful of names, and the list is bounded by max_names.
constexpr size_t max_names = 1024;
const char *interned[max_names];
size_t ninterned;

const char *intern(const char *name, size_t len)
{
    for (size_t i = 0; i < ninterned; i++) {
        if (strlen(interned[i]) == len && !memcmp(interned[i], name, len)) {
            return interned[i];
        }
    }
    if (ninterned == max_names) {
        return nullptr;
    }
    char *copy = static_cast<char *>(malloc(len + 1));
    if (!copy) {
        return nullptr;
    }
    memcpy(copy, name, len);
    copy[len] = '\0';
    interned[ninterned++] = copy;
    return copy;
}

const char *c_name = "C";

bool ascii_alnum(char ch)
{
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
        (ch >= '0' && ch <= '9');
}

char ascii_upper(char ch)
{
    return ch >= 'a' && ch <= 'z' ? ch - 'a' + 'A' : ch;
}

// Does a requested codeset name the UTF-8 that glibc's C.UTF-8 uses? glibc
// accepts it when two spellings agree: the codeset reduced to its letters and
// digits is "utf8" (case-insensitive), and the codeset with only letters,
// digits and "_-.,:" kept, upper-cased, is "UTF8" or "UTF-8". So "UTF-8",
// "utf8" and "UTF+8" match, and "U-T-F-8" and "utf_8" do not.
bool codeset_is_utf8(const char *cs, size_t len)
{
    char norm[5], kept[6];
    size_t n = 0, k = 0;
    for (size_t i = 0; i < len; i++) {
        char ch = cs[i];
        if (ascii_alnum(ch)) {
            if (n == sizeof(norm) - 1) {
                return false;
            }
            norm[n++] = ascii_upper(ch);
        }
        if (ascii_alnum(ch) || ch == '_' || ch == '-' || ch == '.' ||
            ch == ',' || ch == ':') {
            if (k == sizeof(kept) - 1) {
                return false;
            }
            kept[k++] = ascii_upper(ch);
        }
    }
    norm[n] = '\0';
    kept[k] = '\0';
    return !strcmp(norm, "UTF8") &&
        (!strcmp(kept, "UTF8") || !strcmp(kept, "UTF-8"));
}

// Resolve one category's locale name. On success store the name to report
// in *out and return 0; otherwise return an errno value.
int resolve(const char *name, size_t len, const char **out)
{
    if (len > name_max) {
        return EINVAL;
    }
    // A name is a file name in glibc's locale path: no '/' and no "..".
    if (memchr(name, '/', len)) {
        return EINVAL;
    }
    if (len == 2 && !memcmp(name, "..", 2)) {
        return EINVAL;
    }
    if ((len == 1 && name[0] == 'C') || (len == 5 && !memcmp(name, "POSIX", 5))) {
        *out = c_name;
        return 0;
    }
    // language[_territory][.codeset][@modifier], split as glibc splits it:
    // the language ends at '_', '.' or '@', the territory at '.' or '@', the
    // codeset at '@'. C.UTF-8 is the language "C" with any territory and
    // modifier, and a UTF-8 codeset.
    const char *p = name, *end = name + len;
    while (p < end && *p != '_' && *p != '.' && *p != '@') {
        p++;
    }
    if (p - name != 1 || name[0] != 'C') {
        return ENOENT;
    }
    if (p < end && *p == '_') {
        while (p < end && *p != '.' && *p != '@') {
            p++;
        }
    }
    if (p == end || *p != '.') {
        return ENOENT;
    }
    const char *cs = ++p;
    while (p < end && *p != '@') {
        p++;
    }
    if (p == cs || !codeset_is_utf8(cs, p - cs)) {
        return ENOENT;
    }
    const char *s = intern(name, len);
    if (!s) {
        return ENOMEM;
    }
    *out = s;
    return 0;
}

bool name_is_utf8(const char *name)
{
    return name && strcmp(name, "C") != 0;
}

struct __locale_struct make_global()
{
    struct __locale_struct g = *__c_locale_ptr;
    for (int i = 0; i < NCAT; i++) {
        g.__names[i] = c_name;
    }
    return g;
}

struct __locale_struct &global_locale()
{
    static struct __locale_struct g = make_global();
    return g;
}

const char *env_name(int cat)
{
    const char *v = getenv("LC_ALL");
    if (v && *v) {
        return v;
    }
    v = getenv(category_names[cat]);
    if (v && *v) {
        return v;
    }
    v = getenv("LANG");
    if (v && *v) {
        return v;
    }
    return "C";
}

// Fill names[] for the categories in mask from name, which may be composite
// or empty. Return 0 or an errno value; names[] is only valid on success.
int resolve_all(int mask, const char *name, const char *names[NCAT])
{
    if (strchr(name, ';') || !strncmp(name, "LC_", 3)) {
        // Composite: "LC_X=v;LC_Y=w;..." naming every category exactly
        // once (glibc also accepts a repeated name; the last one wins).
        bool seen[NCAT] = {};
        const char *p = name;
        while (*p) {
            const char *eq = strchr(p, '=');
            if (!eq) {
                return EINVAL;
            }
            int cat = -1;
            for (int i = 0; i < NCAT; i++) {
                if (i != LC_ALL && strlen(category_names[i]) == size_t(eq - p) &&
                    !memcmp(category_names[i], p, eq - p)) {
                    cat = i;
                }
            }
            if (cat < 0) {
                return EINVAL;
            }
            const char *v = eq + 1;
            const char *semi = strchr(v, ';');
            size_t vlen = semi ? size_t(semi - v) : strlen(v);
            const char *out;
            int err = resolve(v, vlen, &out);
            if (err) {
                return err;
            }
            if (mask & (1 << cat)) {
                names[cat] = out;
            }
            seen[cat] = true;
            p = semi ? semi + 1 : v + vlen;
        }
        for (int i = 0; i < NCAT; i++) {
            if (i != LC_ALL && !seen[i]) {
                return EINVAL;
            }
        }
        return 0;
    }
    for (int i = 0; i < NCAT; i++) {
        if (i == LC_ALL || !(mask & (1 << i))) {
            continue;
        }
        const char *n = *name ? name : env_name(i);
        int err = resolve(n, strlen(n), &names[i]);
        if (err) {
            return err;
        }
    }
    return 0;
}

// setlocale(LC_ALL, NULL): "C"-style single name when every category
// agrees, otherwise the composite. Called with locale_mutex held.
char all_buf[NCAT * (name_max + 20)];

const char *all_name(const struct __locale_struct *l)
{
    const char *first = l->__names[0];
    bool same = true;
    for (int i = 1; i < NCAT; i++) {
        if (i != LC_ALL && strcmp(l->__names[i], first)) {
            same = false;
        }
    }
    if (same) {
        return first;
    }
    char *p = all_buf;
    for (int i = 0; i < NCAT; i++) {
        if (i == LC_ALL) {
            continue;
        }
        if (p != all_buf) {
            *p++ = ';';
        }
        size_t a = strlen(category_names[i]), b = strlen(l->__names[i]);
        memcpy(p, category_names[i], a);
        p += a;
        *p++ = '=';
        memcpy(p, l->__names[i], b);
        p += b;
    }
    *p = '\0';
    return all_buf;
}

bool is_static(locale_t l)
{
    return l == __c_locale_ptr || l == &global_locale();
}

// The locale a thread is using: its uselocale() object, or the global one.
locale_t current_locale()
{
    locale_t l = __current_locale;
    return (!l || l == LC_GLOBAL_LOCALE) ? &global_locale() : l;
}

} // namespace

extern "C" OSV_LIBC_API
char *setlocale(int category, const char *name) noexcept
{
    if (category < 0 || category >= NCAT) {
        errno = EINVAL;
        return nullptr;
    }
    struct __locale_struct &g = global_locale();
    SCOPE_LOCK(locale_mutex);
    if (!name) {
        return const_cast<char *>(category == LC_ALL ?
            all_name(&g) : g.__names[category]);
    }
    const char *names[NCAT];
    memcpy(names, g.__names, sizeof(names));
    int mask = category == LC_ALL ? all_mask : 1 << category;
    if (category != LC_ALL && (strchr(name, ';') || strchr(name, '='))) {
        errno = ENOENT;
        return nullptr;
    }
    int err = resolve_all(mask, name, names);
    if (err) {
        errno = err;
        return nullptr;
    }
    memcpy(g.__names, names, sizeof(names));
    return const_cast<char *>(category == LC_ALL ?
        all_name(&g) : g.__names[category]);
}

extern "C" OSV_LIBC_API
locale_t __newlocale(int mask, const char *name, locale_t base) noexcept
{
    // libstdc++'s gnu locale model passes 1 << LC_ALL to mean every category,
    // and programs built with musl's <locale.h> pass its LC_ALL_MASK.
    if (mask == 1 << LC_ALL || mask == 0x7fffffff) {
        mask = all_mask;
    }
    if ((mask & ~all_mask) || !name) {
        errno = EINVAL;
        return nullptr;
    }
    if (base == __c_locale_ptr || base == LC_GLOBAL_LOCALE) {
        // The C object is shared and must not change; LC_GLOBAL_LOCALE is
        // not a valid base. Both start from a copy of C.
        base = nullptr;
    }
    const char *names[NCAT];
    {
        SCOPE_LOCK(locale_mutex);
        const struct __locale_struct *from = base ? base : __c_locale_ptr;
        memcpy(names, from->__names, sizeof(names));
        for (int i = 0; i < NCAT; i++) {
            if (!names[i]) {
                names[i] = c_name;
            }
        }
        int err = resolve_all(mask, name, names);
        if (err) {
            errno = err;
            return nullptr;
        }
    }
    bool all_c = true;
    for (int i = 0; i < NCAT; i++) {
        if (i != LC_ALL && strcmp(names[i], "C")) {
            all_c = false;
        }
    }
    if (all_c && !base) {
        return __c_locale_ptr;
    }
    locale_t l = base;
    if (!l) {
        l = static_cast<locale_t>(malloc(sizeof(*l)));
        if (!l) {
            errno = ENOMEM;
            return nullptr;
        }
        *l = *__c_locale_ptr;
    }
    memcpy(l->__names, names, sizeof(names));
    return l;
}
weak_alias(__newlocale, newlocale);

extern "C" OSV_LIBC_API
locale_t __duplocale(locale_t old) noexcept
{
    if (old == __c_locale_ptr) {
        return old;
    }
    locale_t from = old == LC_GLOBAL_LOCALE ? &global_locale() : old;
    locale_t l = static_cast<locale_t>(malloc(sizeof(*l)));
    if (!l) {
        errno = ENOMEM;
        return nullptr;
    }
    SCOPE_LOCK(locale_mutex);
    *l = *from;
    return l;
}
weak_alias(__duplocale, duplocale);

extern "C" OSV_LIBC_API
void __freelocale(locale_t l) noexcept
{
    if (!l || l == LC_GLOBAL_LOCALE || is_static(l)) {
        return;
    }
    free(l);
}
weak_alias(__freelocale, freelocale);

extern "C" OSV_LIBC_API
locale_t __uselocale(locale_t l) noexcept
{
    locale_t old = __current_locale;
    if (!old) {
        old = LC_GLOBAL_LOCALE;
    }
    if (l) {
        __current_locale = l == LC_GLOBAL_LOCALE ? nullptr : l;
    }
    return old;
}
weak_alias(__uselocale, uselocale);

// ---- nl_langinfo ----------------------------------------------------------

namespace {

enum item_kind { K_STR, K_WORD, K_WSTR, K_RATE };

struct item {
    item_kind kind;
    const char *s;
    uintptr_t w;
    const uint32_t *ws;
};

struct override {
    int idx;
    item it;
};

struct category {
    const item *items;
    size_t n;
    const override *utf8;
};

const uint32_t conversion_rate[2] = { 1, 1 };

#define S(x) item{ K_STR, x, 0, nullptr }
#define B(x) item{ K_STR, x, 0, nullptr }
#define D(x) item{ K_WORD, nullptr, x, nullptr }
#define W(x) item{ K_WSTR, nullptr, 0, reinterpret_cast<const uint32_t *>(x) }
#define RATE() item{ K_RATE, nullptr, 0, nullptr }

#include "langinfo-builtin.inc"

#undef S
#undef B
#undef D
#undef W
#undef RATE

char *value(const item &it)
{
    switch (it.kind) {
    case K_STR:
        return const_cast<char *>(it.s);
    case K_WORD:
        return reinterpret_cast<char *>(it.w);
    case K_WSTR:
        return reinterpret_cast<char *>(const_cast<uint32_t *>(it.ws));
    case K_RATE:
        return reinterpret_cast<char *>(const_cast<uint32_t *>(conversion_rate));
    }
    return const_cast<char *>("");
}

} // namespace

extern "C" OSV_LIBC_API
char *__nl_langinfo_l(nl_item item, locale_t l) noexcept
{
    if (!l || l == LC_GLOBAL_LOCALE) {
        l = current_locale();
    }
    int cat = item >> 16;
    int idx = item & 0xffff;
    if (cat < 0 || cat >= NCAT || cat == LC_ALL) {
        return const_cast<char *>("");
    }
    const char *name = l->__names[cat] ? l->__names[cat] : c_name;
    bool utf8 = name_is_utf8(name);
    if (idx == 0xffff) {
        return const_cast<char *>(name);    // _NL_LOCALE_NAME(cat)
    }
    if (cat == LC_CTYPE) {
        if (item == CODESET) {
            return const_cast<char *>(utf8 ? "UTF-8" : "ANSI_X3.4-1968");
        }
        if (item == NL_ITEM(LC_CTYPE, 13)) {    // _NL_CTYPE_MB_CUR_MAX
            return reinterpret_cast<char *>(uintptr_t(utf8 ? MB_CUR_MAX : 1));
        }
        return const_cast<char *>("");
    }
    if (cat == LC_COLLATE) {
        // _NL_COLLATE_NRULES is 0 for both built-in locales; the rest are
        // glibc's internal collation tables, which OSv does not have.
        return idx == 0 ? reinterpret_cast<char *>(uintptr_t(0)) :
            const_cast<char *>("");
    }
    const category &c = builtin[cat];
    if (!c.items || size_t(idx) >= c.n) {
        return const_cast<char *>("");
    }
    if (utf8) {
        for (const override *o = c.utf8; o->idx >= 0; o++) {
            if (o->idx == idx) {
                return value(o->it);
            }
        }
    }
    return value(c.items[idx]);
}
weak_alias(__nl_langinfo_l, nl_langinfo_l);

extern "C" OSV_LIBC_API
char *__nl_langinfo(nl_item item) noexcept
{
    return __nl_langinfo_l(item, current_locale());
}
weak_alias(__nl_langinfo, nl_langinfo);
