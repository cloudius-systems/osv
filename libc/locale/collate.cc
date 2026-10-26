/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// strcoll, strxfrm, wcscoll, wcsxfrm and their _l forms with conf_locale=1.
// A locale whose LC_COLLATE was loaded from glibc-format data collates by its
// tables; C, C.UTF-8 (and a loaded file with no levels) keep byte and code
// point order, as musl's versions, which this file replaces. Written from
// STAGE1-FORMAT-SPEC.md section 6 (learned from localedef's output and from
// glibc's answers; no glibc code), which also gives the rules reproduced here:
//
// A string is split into elements (a character, a contraction such as cs_CZ
// "ch", or an invalid byte), each with a weight index. At each level an
// element has a (possibly empty) weight sequence and a rule from its section:
// backward, position. strxfrm emits the levels in order, separated by 1;
// strcoll compares the same per-level sequences, level by level, with three
// differences glibc 2.41 has (measured) and OSv keeps, so that its strcoll
// orders exactly as glibc's: they decide index order in PostgreSQL. So, as in
// glibc, strcmp of two strxfrm results can disagree with strcoll.
//
// The tables were validated when the locale was loaded (locale-data.cc), so
// every lookup here stays within the data. Each string's elements are found
// once, into a buffer on the stack. The threshold is the two strings' lengths
// in units_len units: bytes for strcoll/strxfrm (narrow), wide characters
// for wcscoll/wcsxfrm (wide). Up to 4096 of those units (16 KB) use the
// stack (two 2 KB ASCII strings in a strcoll, more for other text); longer
// use one malloc(). Every level is then O(n). If malloc() fails the elements
// are found again on each use instead (correct, slower). Note: a narrow
// string of 4096 multi-byte characters exceeds 4096 bytes and allocates.

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "locale-data.h"

#ifndef OSV_COLLATE_HOST
#include <osv/export.h>

#include "libc/internal/locale_impl.h"
#include "libc/internal/libc.h"
#endif

namespace {

uint32_t rd32(const unsigned char *p)
{
    uint32_t w;
    memcpy(&w, p, sizeof(w));
    return w;
}

// The element at s (not at its end) and its length in units. The lists end in
// an entry that always matches, and no list unit is 0, so a comparison never
// passes the string's terminator.
struct mb {
    using unit = unsigned char;
    static uint32_t find(const osv_collate *c, const unit *s, size_t *len)
    {
        int32_t v = int32_t(rd32(c->table_mb + 4 * size_t(s[0])));
        if (v >= 0) {
            *len = 1;
            return uint32_t(v);
        }
        const unsigned char *p = c->extra_mb + (0u - uint32_t(v));
        for (;;) {
            int32_t i = int32_t(rd32(p));
            size_t l = p[4];
            const unsigned char *seq = p + 5;
            size_t k = 0;
            if (i >= 0) {
                while (k < l && s[1 + k] == seq[k]) {
                    k++;
                }
                if (k == l) {
                    *len = 1 + l;
                    return uint32_t(i);
                }
                p = seq + l;
            } else {
                while (k + 1 < l && s[1 + k] == seq[k]) {
                    k++;
                }
                if (k + 1 == l && s[l] >= seq[l - 1] && s[l] <= seq[2 * l - 1]) {
                    *len = 1 + l;
                    size_t at = size_t(0u - uint32_t(i)) + (s[l] - seq[l - 1]);
                    return rd32(c->indirect_mb + 4 * at);
                }
                p = seq + 2 * l;
            }
            p = c->extra_mb + ((size_t(p - c->extra_mb) + 3) & ~size_t(3));
        }
    }
    // The weights of index idx at level lev: *n units at the result.
    static const unsigned char *weights(const osv_collate *c, uint32_t idx, unsigned lev,
                                        size_t *n)
    {
        const unsigned char *p = c->weight_mb + (idx & 0xffffff);
        for (unsigned l = 0; l < lev; l++) {
            p += 1 + p[0];
        }
        *n = p[0];
        return p + 1;
    }
    static uint32_t weight_unit(const unsigned char *w, size_t k)
    {
        return w[k];
    }
    // A position count, as UTF-8 (the 5- and 6-byte forms past 0x1fffff,
    // as glibc writes them; measured). ponytail: a count above 0x7fffffff
    // (2^31 ignorable characters in one string) is written as 0x7fffffff.
    static size_t count_units(size_t n, uint32_t *out)
    {
        uint32_t v = n > 0x7fffffff ? 0x7fffffff : uint32_t(n);
        if (v < 0x80) {
            out[0] = v;
            return 1;
        }
        size_t len = v < 0x800 ? 2 : v < 0x10000 ? 3 : v < 0x200000 ? 4 : v < 0x4000000 ? 5 : 6;
        for (size_t k = len - 1; k > 0; k--) {
            out[k] = 0x80 | (v & 0x3f);
            v >>= 6;
        }
        out[0] = (0xff00u >> len) & 0xff;
        out[0] |= v;
        return len;
    }
};

struct wc {
    using unit = wchar_t;
    static uint32_t find(const osv_collate *c, const unit *s, size_t *len)
    {
        int32_t v = int32_t(osv_table_word(c->table_wc, uint32_t(s[0])));
        if (v >= 0) {
            *len = 1;
            return uint32_t(v);
        }
        const unsigned char *p = c->extra_wc + 4 * size_t(0u - uint32_t(v));
        for (;;) {
            int32_t i = int32_t(rd32(p));
            size_t l = rd32(p + 4);
            const unsigned char *seq = p + 8;
            size_t k = 0;
            if (i >= 0) {
                while (k < l && uint32_t(s[1 + k]) == rd32(seq + 4 * k)) {
                    k++;
                }
                if (k == l) {
                    *len = 1 + l;
                    return uint32_t(i);
                }
                p = seq + 4 * l;
            } else {
                while (k + 1 < l && uint32_t(s[1 + k]) == rd32(seq + 4 * k)) {
                    k++;
                }
                // s[l] only once s[1..l-1] matched: it is at most the terminator.
                uint32_t u = k + 1 == l ? uint32_t(s[l]) : 0, lo = rd32(seq + 4 * (l - 1)),
                         hi = rd32(seq + 4 * (2 * l - 1));
                if (k + 1 == l && u >= lo && u <= hi) {
                    *len = 1 + l;
                    size_t at = size_t(0u - uint32_t(i)) + (u - lo);
                    return rd32(c->indirect_wc + 4 * at);
                }
                p = seq + 8 * l;
            }
        }
    }
    static const unsigned char *weights(const osv_collate *c, uint32_t idx, unsigned lev,
                                        size_t *n)
    {
        const unsigned char *p = c->weight_wc + 4 * size_t(idx & 0xffffff);
        for (unsigned l = 0; l < lev; l++) {
            p += 4 + 4 * size_t(rd32(p));
        }
        *n = rd32(p);
        return p + 4;
    }
    static uint32_t weight_unit(const unsigned char *w, size_t k)
    {
        return rd32(w + 4 * k);
    }
    static size_t count_units(size_t n, uint32_t *out)
    {
        out[0] = n > 0x7fffffff ? 0x7fffffff : uint32_t(n);
        return 1;
    }
};

template <typename T> size_t units_len(const typename T::unit *s);
template <> size_t units_len<mb>(const unsigned char *s)
{
    return strlen(reinterpret_cast<const char *>(s));
}
template <> size_t units_len<wc>(const wchar_t *s)
{
    return wcslen(s);
}

// The elements of a string: their weight indices in v[0..n), or, if no
// memory could be had (v null), found again from the start when an earlier
// one is needed. ponytail: that fallback is O(n^2) per level in backward runs.
template <typename T> struct elems {
    using unit = typename T::unit;
    const osv_collate *c;
    const unit *s;
    uint32_t *v = nullptr;
    size_t n = 0;
    // The rescan cursor: element ck starts at cp.
    size_t ck = 0;
    const unit *cp = nullptr;

    void fill(uint32_t *buf)
    {
        v = buf;
        size_t len;
        for (const unit *p = s; *p; p += len) {
            v[n++] = T::find(c, p, &len);
        }
    }
    void count()
    {
        size_t len;
        for (const unit *p = s; *p; p += len, n++) {
            T::find(c, p, &len);
        }
        cp = s;
    }
    uint32_t get(size_t k)
    {
        if (v) {
            return v[k];
        }
        size_t len;
        if (k < ck) {
            ck = 0;
            cp = s;
        }
        for (; ck < k; ck++) {
            T::find(c, cp, &len);
            cp += len;
        }
        return T::find(c, cp, &len);
    }
};

// The element buffer of one call: the stack for up to 4096 units of the
// strings together (units_len units: bytes for the narrow path, wide
// characters for the wide path), else one malloc().
struct elem_buffer {
    static constexpr size_t stack_max = 4096;
    uint32_t stack[stack_max];
    uint32_t *heap = nullptr;
    // Room for need entries, or null.
    uint32_t *get(size_t need)
    {
        if (need <= stack_max) {
            return stack;
        }
        if (need <= SIZE_MAX / sizeof(uint32_t)) {
            heap = static_cast<uint32_t *>(malloc(need * sizeof(uint32_t)));
        }
        return heap;
    }
    ~elem_buffer()
    {
        free(heap);
    }
};

unsigned rule(const osv_collate *c, uint32_t idx, unsigned lev)
{
    return c->rules[size_t(idx >> 24) * c->nrules + lev];
}

constexpr unsigned backward = 2, position = 4;
constexpr size_t none = SIZE_MAX;

// The units of one string at one level, in the order of section 6.4 (strxfrm)
// or 6.5 (strcoll), as a stream: next() gives the next unit or -1.
template <typename T> struct level_units {
    const osv_collate *c;
    elems<T> &e;
    unsigned lev;
    bool coll, pos;
    size_t i = 0;                   // the next element of the forward scan
    size_t run_start = 0, run_next = 0, skip = none;    // a backward run, emitted downwards
    size_t ignored = 0;             // ignorable elements since the last weight (position)
    uint32_t pending[6];
    size_t npending = 0, ipending = 0;
    const unsigned char *w = nullptr;
    size_t nw = 0, iw = 0;

    level_units(const osv_collate *c, elems<T> &e, unsigned lev, bool coll, bool pos)
        : c(c), e(e), lev(lev), coll(coll), pos(pos)
    {
    }

    size_t next_element()
    {
        for (;;) {
            while (run_next > run_start) {
                size_t k = --run_next;
                if (k != skip) {
                    return k;
                }
            }
            if (i >= e.n) {
                return none;
            }
            if (!(rule(c, e.get(i), lev) & backward)) {
                return i++;
            }
            size_t j = i + 1;
            while (j < e.n && (rule(c, e.get(j), lev) & backward)) {
                j++;
            }
            run_start = i;
            run_next = j;
            skip = none;
            if (coll) {
                // strcoll: a run of two or more with an element after it
                // skips its second-to-last element (measured).
                if (j < e.n && j - i >= 2) {
                    skip = j - 2;
                }
            } else if (pos && j == e.n && e.n < 4096) {
                // strxfrm at a position level: a run reaching the end of a
                // string of fewer than 4096 elements skips its last element
                // (measured; at 4096 or more glibc keeps it).
                skip = j - 1;
            }
            i = j;
        }
    }

    int64_t next()
    {
        for (;;) {
            if (ipending < npending) {
                return pending[ipending++];
            }
            if (iw < nw) {
                return T::weight_unit(w, iw++);
            }
            size_t k = next_element();
            if (k == none) {
                return -1;
            }
            w = T::weights(c, e.get(k), lev, &nw);
            iw = 0;
            if (pos) {
                if (!nw) {
                    ignored++;
                    continue;
                }
                npending = T::count_units(ignored + 1, pending);
                ipending = 0;
                ignored = 0;
            }
        }
    }
};

template <typename T>
int coll(const osv_collate *c, const typename T::unit *a, const typename T::unit *b)
{
    elems<T> ea{c, a}, eb{c, b};
    size_t la = units_len<T>(a), lb = units_len<T>(b);
    elem_buffer buf;
    // The element counts are at most the lengths; if they do not fit even the
    // sum overflows nothing: get() fails and both are rescanned.
    uint32_t *v = la <= SIZE_MAX - lb ? buf.get(la + lb) : nullptr;
    if (v) {
        ea.fill(v);
        eb.fill(v + ea.n);
    } else {
        ea.count();
        eb.count();
    }
    // The empty string is first, even before one of ignorable characters
    // (measured).
    if (!ea.n || !eb.n) {
        return ea.n ? 1 : eb.n ? -1 : 0;
    }
    for (unsigned lev = 0; lev < c->nrules; lev++) {
        // Both strings follow the position rule of a's first element (measured).
        bool pos = rule(c, ea.get(0), lev) & position;
        level_units<T> ua(c, ea, lev, true, pos), ub(c, eb, lev, true, pos);
        for (;;) {
            int64_t x = ua.next(), y = ub.next();
            if (x != y) {
                return x < y ? -1 : 1;
            }
            if (x < 0) {
                break;
            }
        }
    }
    return 0;
}

template <typename T>
size_t xfrm(const osv_collate *c, typename T::unit *dest, const typename T::unit *src, size_t n)
{
    elems<T> e{c, src};
    elem_buffer buf;
    if (uint32_t *v = buf.get(units_len<T>(src))) {
        e.fill(v);
    } else {
        e.count();
    }
    size_t out = 0;
    auto put = [&](uint32_t u) {
        if (out < n) {
            dest[out] = static_cast<typename T::unit>(u);
        }
        out++;
    };
    for (unsigned lev = 0; e.n && lev < c->nrules; lev++) {
        if (lev) {
            put(1);
        }
        size_t level_start = out;
        level_units<T> u(c, e, lev, false, rule(c, e.get(0), lev) & position);
        for (int64_t x; (x = u.next()) >= 0;) {
            put(uint32_t(x));
        }
        // A last level that wrote nothing loses its separator, unless the
        // separator is all there is (measured).
        if (lev && lev + 1 == c->nrules && out == level_start && out > 1) {
            out--;
        }
    }
    if (out < n) {
        dest[out] = 0;
    }
    return out;
}

} // namespace

int osv_strcoll(const osv_collate *c, const char *a, const char *b) noexcept
{
    return coll<mb>(c, reinterpret_cast<const unsigned char *>(a),
                    reinterpret_cast<const unsigned char *>(b));
}

size_t osv_strxfrm(const osv_collate *c, char *dest, const char *src, size_t n) noexcept
{
    return xfrm<mb>(c, reinterpret_cast<unsigned char *>(dest),
                    reinterpret_cast<const unsigned char *>(src), n);
}

int osv_wcscoll(const osv_collate *c, const wchar_t *a, const wchar_t *b) noexcept
{
    return coll<wc>(c, a, b);
}

size_t osv_wcsxfrm(const osv_collate *c, wchar_t *dest, const wchar_t *src, size_t n) noexcept
{
    return xfrm<wc>(c, dest, src, n);
}

#ifndef OSV_COLLATE_HOST
// The entry points (on the host the differential calls the osv_ functions).

namespace {
// The loaded LC_COLLATE of l, if it has levels.
const osv_collate *loaded(locale_t l)
{
    auto d = static_cast<const osv_locale_data *>(__osv_collate_data(l));
    return d && d->collate.nrules ? &d->collate : nullptr;
}
}

extern "C" OSV_LIBC_API
int __strcoll_l(const char *a, const char *b, locale_t l) noexcept
{
    const osv_collate *c = loaded(l);
    return c ? osv_strcoll(c, a, b) : strcmp(a, b);
}
weak_alias(__strcoll_l, strcoll_l);

extern "C" OSV_LIBC_API
int strcoll(const char *a, const char *b) noexcept
{
    return __strcoll_l(a, b, nullptr);
}

extern "C" OSV_LIBC_API
size_t __strxfrm_l(char *__restrict dest, const char *__restrict src, size_t n,
                   locale_t l) noexcept
{
    if (const osv_collate *c = loaded(l)) {
        return osv_strxfrm(c, dest, src, n);
    }
    size_t len = strlen(src);
    if (n > len) {
        memcpy(dest, src, len + 1);
    }
    return len;
}
weak_alias(__strxfrm_l, strxfrm_l);

extern "C" OSV_LIBC_API
size_t strxfrm(char *__restrict dest, const char *__restrict src, size_t n) noexcept
{
    return __strxfrm_l(dest, src, n, nullptr);
}

extern "C" OSV_LIBC_API
int __wcscoll_l(const wchar_t *a, const wchar_t *b, locale_t l) noexcept
{
    const osv_collate *c = loaded(l);
    return c ? osv_wcscoll(c, a, b) : wcscmp(a, b);
}
weak_alias(__wcscoll_l, wcscoll_l);

extern "C" OSV_LIBC_API
int wcscoll(const wchar_t *a, const wchar_t *b) noexcept
{
    return __wcscoll_l(a, b, nullptr);
}

extern "C" OSV_LIBC_API
size_t __wcsxfrm_l(wchar_t *__restrict dest, const wchar_t *__restrict src, size_t n,
                   locale_t l) noexcept
{
    if (const osv_collate *c = loaded(l)) {
        return osv_wcsxfrm(c, dest, src, n);
    }
    // musl's: copy, truncated to n - 1 units and terminated if it does not fit.
    size_t len = wcslen(src);
    if (len < n) {
        wmemcpy(dest, src, len + 1);
    } else if (n) {
        wmemcpy(dest, src, n - 1);
        dest[n - 1] = 0;
    }
    return len;
}
weak_alias(__wcsxfrm_l, wcsxfrm_l);

extern "C" OSV_LIBC_API
size_t wcsxfrm(wchar_t *__restrict dest, const wchar_t *__restrict src, size_t n) noexcept
{
    return __wcsxfrm_l(dest, src, n, nullptr);
}
#endif
