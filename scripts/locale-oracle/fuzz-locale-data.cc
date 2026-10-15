/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Fuzz OSv's locale data loader (libc/locale/locale-data.cc) and the code
// that reads what it accepts on Linux under ASan and UBSan. Build and run:
//
//   scripts/locale-oracle/time-diff/build.sh    (builds time-diff/fuzz-locale-data)
//   fuzz-locale-data <iterations> <rng-seed> <locale-dir>... [--archive <locale-archive>]
//
// build.sh compiles OSv's strftime.c, strptime.c, strfmon.c and rpmatch.c
// for Linux (their names prefixed osv_) with the loader and this file, which
// supplies the two functions those files read the locale through
// (__nl_langinfo_l and __osv_nl_list): an accepted LC_TIME, LC_MONETARY or
// LC_MESSAGES is read through the loader's accessors, as OSv's
// libc/locale/locale.cc does, and the other categories are C's. Each
// accepted one is fed to strftime, wcsftime, strptime (round trips and
// fixed inputs), strfmon, localeconv's reads and rpmatch; a consumer that
// leaves its buffers is an ASan report, and one that does not finish in 10 s
// is a failure.
//
// Each <locale-dir> is a localedef output directory (de_DE.utf8); its twelve
// category files are seeds, and so is a small archive the harness builds from
// them and, if given, a real locale-archive. Each input is a seed mutated in
// place (bit flips, words set to boundary values in the header, string index
// and archive tables, truncation by poisoning the tail): the loader must
// either reject it or accept it with every item readable as its type within
// the data, which the harness then reads (an overrun is an ASan report).
// Deterministic cases first: every seed is accepted unchanged (the positive
// control) and each named corruption is rejected (the negative control).
// "--self-test" reads items of a truncated file without validating it and
// must die under ASan, showing the harness detects what the checks prevent.
// Bit flips cannot make a long string, so one input in eight is a category
// with string items pointed at new strings appended to it (runs of a byte,
// random bytes, chains of locale conversions, ERA lists) up to 6000 bytes.

#include "locale-data.h"

#include <sanitizer/asan_interface.h>

#include <langinfo.h>
#include <locale.h>
#include <monetary.h>
#include <wchar.h>

#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

namespace {

const char *const file_names[13] = {
    "LC_CTYPE", "LC_NUMERIC", "LC_TIME", "LC_COLLATE", "LC_MONETARY",
    "LC_MESSAGES/SYS_LC_MESSAGES", nullptr, "LC_PAPER", "LC_NAME",
    "LC_ADDRESS", "LC_TELEPHONE", "LC_MEASUREMENT", "LC_IDENTIFICATION",
};

// The item types the harness reads back, kept apart from the loader's table:
// the measured types (scripts/locale-oracle/item-types/).
const char *const types[13] = {
    "xxxxxxxxxxssxdsxxdddssssssssssdwwwwwwwwwwssssssssssdddddddddddxwxwdwdxdd",
    "sssdds",
    "ssssssssssssssssssssssssssssssssssssssssssssssssssdxwwwwwwwwwwwwwwwwwwww"
    "wwwwwwwwwwwwwwwwwwwwwwwwwwwwwbdbbbbsswssssssssssssswwwwwwwwwwwwsssssssss"
    "ssswwwwwwwwwwww",
    "dxxxxxxxxxxxxdxxxxs",
    "sssssssbbbbbbbbsbbbbbbssbbbbbbbbbbbbbbddddrdds",
    "sssss",
    "",
    "dds",
    "sssssss",
    "ssssssdssssss",
    "sssss",
    "bs",
    "ssssssssssssssss",
};

struct seed {
    std::string what;
    int cat;            // -1: an archive
    std::vector<std::string> names;    // locale names in an archive
    char *buf;          // malloc'd, exactly size bytes
    size_t size;
};

std::vector<seed> seeds;
unsigned long accepted, rejected, inputs;
unsigned long last_found;    // categories an archive input yielded
volatile unsigned long sink;

char *read_file(const std::string &path, size_t *size)
{
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) {
        return nullptr;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = static_cast<char *>(malloc(n));
    if (fread(b, 1, n, f) != size_t(n)) {
        exit(2);
    }
    fclose(f);
    *size = n;
    return b;
}

uint32_t rd(const char *p)
{
    uint32_t w;
    memcpy(&w, p, 4);
    return w;
}

void wr(char *p, uint32_t w)
{
    memcpy(p, &w, 4);
}

// ---- the consumers ----

extern "C" {
size_t osv___strftime_l(char *, size_t, const char *, const struct tm *, locale_t);
size_t osv___wcsftime_l(wchar_t *, size_t, const wchar_t *, const struct tm *, locale_t);
char *osv_strptime(const char *, const char *, struct tm *);
ssize_t osv_strfmon(char *, size_t, const char *, ...);
int osv_rpmatch(const char *);
}

const osv_locale_data *cur[13];    // the category being consumed, or C's
unsigned long consumed[13], long_items;    // inputs fed to the consumers; with an item >= 2048 bytes

// The C locale (the consumers' C_LOCALE, always C's items) and the fuzzed
// one (glibc's C.UTF-8 object for the case maps, with cur[]'s items). They
// must be different objects: glibc's newlocale(LC_ALL_MASK, "C", 0) returns
// the same one every time, which once made the consumers read only C's data.
locale_t c_locale()
{
    static locale_t c = newlocale(LC_ALL_MASK, "C", nullptr);
    return c;
}

locale_t fuzz_locale()
{
    static locale_t u = newlocale(LC_ALL_MASK, "C.UTF-8", nullptr);
    if (!u || u == c_locale()) {
        abort();
    }
    return u;
}

} // namespace

extern "C" char *osv_shim_nl_langinfo_l(nl_item it, locale_t l)
{
    int cat = it >> 16;
    if (l != c_locale() && cat >= 0 && cat < 13 && cur[cat]) {
        return osv_locale_item(cur[cat], it & 0xffff);
    }
    return nl_langinfo_l(it, c_locale());
}

extern "C" const char *__osv_nl_list(nl_item it, unsigned i, locale_t l)
{
    int cat = it >> 16;
    if (l != c_locale() && cat >= 0 && cat < 13 && cur[cat]) {
        return osv_locale_list(cur[cat], it & 0xffff, i);
    }
    return i ? nullptr : nl_langinfo_l(it, c_locale());
}

extern "C" locale_t __osv_c_locale(void)
{
    return c_locale();
}

namespace {

void too_slow(int)
{
    static const char m[] = "FAIL: a consumer did not finish in 10 s\nSUMMARY: 1 failures\n";
    if (write(1, m, sizeof(m) - 1)) {
    }
    _exit(1);
}

// Run the consumers over an accepted LC_TIME, LC_MONETARY or LC_MESSAGES.
void consume(const osv_locale_data &d)
{
    if (d.cat != LC_TIME && d.cat != LC_MONETARY && d.cat != LC_MESSAGES) {
        return;
    }
    cur[d.cat] = &d;
    consumed[d.cat]++;
    for (uint32_t i = 0; i < d.nstrings && i < strlen(types[d.cat]); i++) {
        if (types[d.cat][i] == 's' && strlen(osv_locale_item(&d, int(i))) >= 2048) {
            long_items++;
            break;
        }
    }
    locale_t old = uselocale(fuzz_locale());
    locale_t L = fuzz_locale();
    alarm(10);
    unsigned long acc = 0;
    if (d.cat == LC_TIME) {
        static const char *const fmts[] = { "%c", "%Ec", "%x", "%Ex", "%X", "%EX", "%r", "%EC",
            "%Ey", "%EY", "%_EY", "%Oy", "%OH", "%Om", "%Od", "%OB", "%Ob", "%a %A %b %B %p %P",
            "%^c", "%#Ec", "%30c", "%-Ey|%EC|%Oe", "%D %T %F %R %s" };
        static const wchar_t *const wfmts[] = { L"%c", L"%Ec", L"%EY", L"%x", L"%r", L"%OB",
            L"%^EC", L"%\u00e9" };
        static const char *const ins[] = { "", "x", "2026", " 1 2 3", "13:05:09", "AM PM",
            "\xe4\xbb\xa4\xe5\x92\x8c 8\xe5\xb9\xb4", "12345678901234567890" };
        char *buf = static_cast<char *>(malloc(256));
        wchar_t *wb = static_cast<wchar_t *>(malloc(256 * sizeof(wchar_t)));
        for (int y : { 126, -1800, 8100 }) {
            struct tm tm = {};
            tm.tm_year = y;
            tm.tm_mon = 6;
            tm.tm_mday = 15;
            tm.tm_hour = 13;
            tm.tm_min = 5;
            tm.tm_sec = 9;
            tm.tm_wday = 3;
            tm.tm_yday = 195;
            for (const char *f : fmts) {
                for (size_t n : { 0, 1, 7 }) {
                    acc += osv___strftime_l(buf, n, f, &tm, L);
                }
                size_t r = osv___strftime_l(buf, 256, f, &tm, L);
                if (r) {
                    struct tm p = {};
                    const char *e = osv_strptime(buf, f, &p);
                    acc += e ? e - buf : 0;
                }
            }
            for (const wchar_t *f : wfmts) {
                acc += osv___wcsftime_l(wb, 256, f, &tm, L);
                acc += osv___wcsftime_l(wb, 3, f, &tm, L);
            }
        }
        for (const char *in : ins) {
            for (const char *f : fmts) {
                struct tm p = {};
                acc += osv_strptime(in, f, &p) != nullptr;
            }
        }
        free(wb);
        free(buf);
    } else if (d.cat == LC_MONETARY) {
        static const char *const fmts[] = { "%n", "%i", "%!n", "%^n", "%(n", "%+i", "%#10n",
            "%=*#12.3n", "%-30i", "%25n", "%.0n", "%#3.5i" };
        for (size_t n : { 0, 16, 8192 }) {
            char *b = static_cast<char *>(malloc(n ? n : 1));
            for (const char *f : fmts) {
                for (double v : { 0.0, -1234.56, 1234567.891, 1e308, -1e15 }) {
                    acc += osv_strfmon(b, n, f, v);
                }
            }
            free(b);
        }
        // localeconv() in libc/locale/locale.cc is these reads: the
        // pointers of items 0..6 and the first byte of 7..14 and 16..21
        // (it is not built here; it needs the rest of locale.cc).
        for (int i = 0; i < 7; i++) {
            acc += strlen(osv_locale_item(&d, i));
        }
        for (int i = 7; i < 22; i++) {
            acc += i == 15 ? 0 : static_cast<unsigned char>(*osv_locale_item(&d, i));
        }
    } else {
        for (const char *in : { "y", "n", "ja", "", "yes", "\xd0\xb4\xd0\xb0", "+", "0",
                                "nnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnn" }) {
            acc += osv_rpmatch(in) + 1;
        }
    }
    alarm(0);
    uselocale(old);
    cur[d.cat] = nullptr;
    sink += acc;
}

// Read every item of an accepted category as its type: strings to their NUL,
// wide strings to their 0, words and bytes. Any read outside the data is an
// ASan report. Also check osv_locale_item() agrees with the index.
void read_items(const osv_locale_data &d)
{
    const char *t = types[d.cat];
    size_t nt = strlen(t);
    unsigned long acc = 0;
    for (uint32_t i = 0; i < d.nstrings; i++) {
        uint32_t off = rd(d.base + 8 + 4 * size_t(i));
        char type = i < nt ? t[i] : 'x';
        char *v = osv_locale_item(&d, int(i));
        const char *p = d.base + off;
        switch (type) {
        case 's':
            acc += strlen(v);
            if (v != p) {
                abort();
            }
            break;
        case 'w': {
            const uint32_t *w = reinterpret_cast<const uint32_t *>(v);
            while (*w) {
                acc += *w++;
            }
            break;
        }
        case 'd':
            acc += uintptr_t(v);
            if (uintptr_t(v) != rd(p)) {
                abort();
            }
            break;
        case 'r':
            acc += reinterpret_cast<const uint32_t *>(v)[0] + reinterpret_cast<const uint32_t *>(v)[1];
            break;
        case 'b':
            acc += static_cast<unsigned char>(*v);
            break;
        default:
            if (off < d.size) {
                acc += static_cast<unsigned char>(*v);
            }
        }
    }
    acc += *osv_locale_item(&d, int(d.nstrings)) + *osv_locale_item(&d, 0xffff) +
        *osv_locale_item(&d, -1);
    sink += acc;
    consume(d);
}

// Try one input: a category or an archive. Return whether it was accepted.
bool run(const seed &s, size_t size)
{
    inputs++;
    osv_locale_data d;
    bool ok = false;
    if (s.cat >= 0) {
        ok = osv_locale_check(s.cat, s.buf, size, &d);
        if (ok) {
            read_items(d);
        }
    } else {
        last_found = 0;
        for (const std::string &n : s.names) {
            for (int c = 0; c < 13; c++) {
                if (c != 6 && osv_locale_archive_find(s.buf, size, n.c_str(), c, &d)) {
                    if (d.base < s.buf || d.base + d.size > s.buf + size || d.cat != c) {
                        abort();
                    }
                    read_items(d);
                    ok = true;
                    last_found++;
                }
            }
        }
        // A name that is not there, and one that is too long for any entry.
        if (osv_locale_archive_find(s.buf, size, "xx_YY.utf8", 1, &d)) {
            abort();
        }
    }
    (ok ? accepted : rejected)++;
    return ok;
}

// A small archive of the seed locales' small categories: header, name
// table, names, data, records.
seed build_archive(const std::vector<std::pair<std::string, std::vector<seed *>>> &locs)
{
    std::string img(56, '\0');
    auto align = [&]() { while (img.size() % 4) img.push_back('\0'); };
    // A prime above twice the names (at least 7), so probing collides and
    // wraps and every slot is reachable.
    uint32_t hsize = 2 * locs.size() + 1 < 7 ? 7 : 2 * locs.size() + 1;
    for (bool prime = false; !prime; hsize += prime ? 0 : 2) {
        prime = true;
        for (uint32_t d = 3; d * d <= hsize; d += 2) {
            prime &= hsize % d != 0;
        }
    }
    uint32_t hoff = img.size();
    img.append(12 * hsize, '\0');
    std::vector<uint32_t> name_off, rec_off;
    for (auto &l : locs) {
        name_off.push_back(img.size());
        img += l.first;
        img.push_back('\0');
    }
    align();
    std::vector<std::vector<std::pair<uint32_t, uint32_t>>> spans(locs.size());
    for (size_t i = 0; i < locs.size(); i++) {
        spans[i].assign(13, { 0, 0 });
        for (seed *s : locs[i].second) {
            if (s->cat == 0 || s->cat == 3) {
                continue;   // the big tables; the category seeds cover them
            }
            spans[i][s->cat] = { uint32_t(img.size()), uint32_t(s->size) };
            img.append(s->buf, s->size);
            align();
        }
    }
    for (size_t i = 0; i < locs.size(); i++) {
        rec_off.push_back(img.size());
        img.append(4 + 13 * 8, '\0');
        wr(&img[rec_off[i]], 1);
        for (int c = 0; c < 13; c++) {
            wr(&img[rec_off[i] + 4 + 8 * c], spans[i][c].first);
            wr(&img[rec_off[i] + 8 + 8 * c], spans[i][c].second);
        }
    }
    wr(&img[0], 0xde020109);
    wr(&img[8], hoff);
    wr(&img[12], locs.size());
    wr(&img[16], hsize);
    seed a;
    a.what = "built-archive";
    a.cat = -1;
    for (size_t i = 0; i < locs.size(); i++) {
        const std::string &n = locs[i].first;
        uint32_t h = n.size();
        for (unsigned char c : n) {
            h = ((h << 9) | (h >> 23)) + c;
        }
        uint32_t idx = h % hsize, step = 1 + h % (hsize - 2);
        while (rd(&img[hoff + 12 * idx + 4])) {
            idx = (idx + step) % hsize;
        }
        wr(&img[hoff + 12 * idx], h);
        wr(&img[hoff + 12 * idx + 4], name_off[i]);
        wr(&img[hoff + 12 * idx + 8], rec_off[i]);
        a.names.push_back(n);
    }
    a.size = img.size();
    a.buf = static_cast<char *>(malloc(a.size));
    memcpy(a.buf, img.data(), a.size);
    return a;
}

// Undo log for in-place mutation.
struct undo {
    size_t off;
    char old[4];
    size_t n;
};

int fails;

void expect(bool cond, const std::string &what)
{
    if (!cond) {
        printf("FAIL: %s\n", what.c_str());
        fails++;
    }
}

// Apply f to a scratch copy of s and expect rejection.
template <typename F>
void negative(const seed &s, const char *what, F f)
{
    std::vector<char> copy(s.buf, s.buf + s.size);
    size_t size = s.size;
    f(copy.data(), size);
    char *b = static_cast<char *>(malloc(size ? size : 1));
    memcpy(b, copy.data(), size);
    seed t = s;
    t.buf = b;
    expect(!run(t, size), s.what + ": " + what + " rejected");
    free(b);
}

// Hostile data through the consumers (long monetary strings, nesting
// locale formats): each
// category is accepted, and the consumers must neither leave their buffers
// (ASan) nor take long. A seed's items are pointed at new strings.
seed with_items(const seed &s, const std::vector<std::pair<uint32_t, std::string>> &items)
{
    std::string img(s.buf, s.size);
    for (auto &it : items) {
        while (img.size() % 4) {
            img.push_back('\0');
        }
        wr(&img[8 + 4 * size_t(it.first)], img.size());
        img += it.second;
        img.push_back('\0');
    }
    while (img.size() % 4) {
        img.push_back('\0');
    }
    seed t = s;
    t.size = img.size();
    t.buf = static_cast<char *>(malloc(t.size));
    memcpy(t.buf, img.data(), t.size);
    return t;
}

void consumer_cases(const seed &s)
{
    std::string word(4, '\0');
    std::vector<std::pair<std::string, std::vector<std::pair<uint32_t, std::string>>>> cases;
    if (s.cat == LC_MONETARY) {
        cases.push_back({ "3000-byte mon_decimal_point", { { 2, std::string(3000, 'x') } } });
        cases.push_back({ "3000-byte mon_thousands_sep, grouping 1",
                          { { 3, std::string(3000, 'y') }, { 4, "\1" } } });
    } else if (s.cat == LC_TIME) {
        std::string c12;
        for (int i = 0; i < 12; i++) {
            c12 += "%c";
        }
        cases.push_back({ "D_T_FMT %c x 12", { { 40, c12 } } });
        // ERA (44) with 8 entries whose format is %EY, ERA_NUM_ENTRIES (50) 8.
        std::string era;
        for (int i = 0; i < 8; i++) {
            era += "+:1:" + std::to_string(2000 + i) + "/01/01:+*:E" + std::to_string(i) + ":%EY";
            era.push_back('\0');
        }
        era.pop_back();
        wr(&word[0], 8);
        cases.push_back({ "8 eras with format %EY", { { 44, era }, { 50, word } } });
    }
    for (auto &c : cases) {
        seed t = with_items(s, c.second);
        if (c.second.size() == 2 && c.second[1].first == 50) {
            // ERA_NUM_ENTRIES is a word item: point it at the word itself.
            uint32_t off = rd(t.buf + 8 + 4 * 50);
            wr(t.buf + 8 + 4 * 50, off);
        }
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        bool ok = run(t, t.size);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        double ms = (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6;
        expect(ok && ms < 1000, s.what + ": " + c.first + " accepted and consumed (" +
               std::to_string(ms) + " ms)");
        printf("consumer %s: %s %s in %.1f ms\n", s.what.c_str(), c.first.c_str(),
               ok ? "consumed" : "rejected", ms);
        free(t.buf);
    }
}

void deterministic()
{
    for (const seed &s : seeds) {
        consumer_cases(s);
    }
    for (const seed &s : seeds) {
        expect(run(s, s.size), s.what + ": unmodified seed accepted");
        if (s.cat < 0) {
            // Every category of every locale; the built archive leaves out
            // LC_CTYPE and LC_COLLATE.
            size_t want = s.names.size() * (s.what == "built-archive" ? 10 : 12);
            expect(last_found == want, s.what + ": " + std::to_string(last_found) + " of " +
                   std::to_string(want) + " categories found");
        }
    }
    for (const seed &s : seeds) {
        if (s.cat < 0) {
            negative(s, "bad magic", [](char *b, size_t &) { b[0] ^= 1; });
            for (uint32_t hs : { 0u, 1u, 2u }) {
                negative(s, ("name table size " + std::to_string(hs)).c_str(),
                         [hs](char *b, size_t &) { wr(b + 16, hs); });
            }
            negative(s, "name table offset past end", [](char *b, size_t &n) { wr(b + 8, n); });
            negative(s, "name table size past end", [](char *b, size_t &n) { wr(b + 16, n / 12 + 1); });
            negative(s, "truncated header", [](char *, size_t &n) { n = 40; });
            negative(s, "every record offset past end", [&s](char *b, size_t &n) {
                uint32_t ho = rd(b + 8), hs = rd(b + 16);
                for (uint32_t i = 0; i < hs; i++) {
                    if (rd(b + ho + 12 * i + 4)) {
                        wr(b + ho + 12 * i + 8, n);
                    }
                }
            });
            negative(s, "every record misaligned", [](char *b, size_t &) {
                uint32_t ho = rd(b + 8), hs = rd(b + 16);
                for (uint32_t i = 0; i < hs; i++) {
                    if (rd(b + ho + 12 * i + 4)) {
                        wr(b + ho + 12 * i + 8, rd(b + ho + 12 * i + 8) + 2);
                    }
                }
            });
            negative(s, "every name offset past end", [](char *b, size_t &n) {
                uint32_t ho = rd(b + 8), hs = rd(b + 16);
                for (uint32_t i = 0; i < hs; i++) {
                    if (rd(b + ho + 12 * i + 4)) {
                        wr(b + ho + 12 * i + 4, n);
                    }
                }
            });
            continue;
        }
        uint32_t n = rd(s.buf + 4);
        negative(s, "bad magic", [](char *b, size_t &) { b[3] ^= 0x80; });
        negative(s, "other category's magic", [&s](char *b, size_t &) {
            wr(b, rd(b) ^ (s.cat == 1 ? 3 : 1));
        });
        negative(s, "size 7", [](char *, size_t &sz) { sz = 7; });
        negative(s, "truncated to the index", [n](char *, size_t &sz) { sz = 8 + 4 * n; });
        // LC_CTYPE and LC_COLLATE end in internal tables OSv does not read
        // (only their offsets are checked), so a lost last byte shows only
        // in the other categories.
        if (s.cat != 0 && s.cat != 3) {
            negative(s, "truncated by one byte", [](char *, size_t &sz) { sz -= 1; });
        }
        negative(s, "nstrings huge", [](char *b, size_t &) { wr(b + 4, 0xffffffff); });
        negative(s, "nstrings past the end", [](char *b, size_t &sz) { wr(b + 4, sz / 4); });
        // The fewest items glibc 2.41 accepts (measured): LC_CTYPE 86, the
        // others every public item.
        negative(s, "nstrings one too few", [&s](char *b, size_t &) {
            wr(b + 4, s.cat == 0 ? 85 : strlen(types[s.cat]) - 1);
        });
        negative(s, "last offset past end", [n](char *b, size_t &sz) { wr(b + 8 + 4 * (n - 1), sz + 1); });
        // A word item moved off alignment, and a string with no NUL.
        const char *t = types[s.cat];
        for (uint32_t i = 0; i < n && i < strlen(t); i++) {
            if (t[i] == 'd' || t[i] == 'w') {
                negative(s, ("misaligned word item " + std::to_string(i)).c_str(),
                         [i](char *b, size_t &) { wr(b + 8 + 4 * i, rd(b + 8 + 4 * i) + 1); });
                break;
            }
        }
        for (uint32_t i = 0; i < n && i < strlen(t); i++) {
            if (t[i] == 's') {
                negative(s, ("unterminated string item " + std::to_string(i)).c_str(),
                         [i](char *b, size_t &sz) {
                             // Point the item at the last byte and make it non-NUL.
                             wr(b + 8 + 4 * i, sz - 1);
                             b[sz - 1] = 'x';
                         });
                break;
            }
        }
    }
}

// One random input: mutate a seed in place, run, undo.
void random_input(std::mt19937_64 &rng)
{
    seed &s = seeds[rng() % seeds.size()];
    std::vector<undo> log;
    auto put = [&](size_t off, const void *src, size_t n) {
        if (off + n > s.size) {
            return;
        }
        undo u;
        u.off = off;
        u.n = n;
        memcpy(u.old, s.buf + off, n);
        log.push_back(u);
        memcpy(s.buf + off, src, n);
    };
    // Where the interesting words are: header and index, or archive tables.
    auto hot_word = [&]() -> size_t {
        if (s.cat >= 0) {
            uint32_t n = rd(s.buf + 4);
            size_t idx_end = 8 + 4 * size_t(n);
            if (idx_end > s.size) {
                idx_end = s.size;
            }
            return (rng() % (idx_end / 4)) * 4;
        }
        // The archive's own tables, read from the seed as mutated so far:
        // every read is bounds-checked, since earlier mutations may have
        // changed the offsets.
        auto rdw = [&](size_t off) -> uint32_t { return off + 4 <= s.size ? rd(s.buf + off) : 0; };
        size_t ho = rdw(8), hs = rdw(16);
        switch (hs ? rng() % 3 : 0) {
        case 0:
            return (rng() % 14) * 4;
        case 1:
            return ho + 4 * (rng() % (3 * hs));
        default: {
            size_t ro = rdw(ho + 12 * (rng() % hs) + 8);
            return ro ? ro + 4 * (rng() % 27) : (rng() % 14) * 4;
        }
        }
    };
    int nmut = 1 + rng() % 4;
    size_t size = s.size;
    for (int m = 0; m < nmut; m++) {
        switch (rng() % 6) {
        case 0: {   // bit flip anywhere
            size_t off = rng() % s.size;
            char c = s.buf[off] ^ char(1 << (rng() % 8));
            put(off, &c, 1);
            break;
        }
        case 1:     // bit flip in a hot word
        case 2: {   // a hot word set to a boundary value
            size_t off = hot_word();
            if (off + 4 > s.size) {
                break;
            }
            uint32_t w = rd(s.buf + off);
            uint32_t vals[] = { 0, 1, 2, 3, uint32_t(s.size), uint32_t(s.size - 1),
                uint32_t(s.size + 1), uint32_t(s.size - 4), 0xffffffff, 0x7fffffff,
                0x80000000, w + 1, w + 2, w - 1, w + 4, w * 2, uint32_t(rng()) };
            uint32_t v = m % 2 && rng() % 2 ? w ^ (1u << (rng() % 32)) :
                vals[rng() % (sizeof(vals) / sizeof(vals[0]))];
            put(off, &v, 4);
            break;
        }
        case 3: {   // a NUL byte removed or inserted at a hot word's target
            size_t off = hot_word();
            if (off + 4 > s.size) {
                break;
            }
            size_t t = rd(s.buf + off);
            if (t < s.size) {
                char c = s.buf[t] ? 0 : 'x';
                put(t, &c, 1);
            }
            break;
        }
        case 4:     // truncation
            size = rng() % 4 ? size - rng() % (size < 64 ? size + 1 : 64) : rng() % (size + 1);
            break;
        default: {  // a random word anywhere
            size_t off = (rng() % (s.size / 4)) * 4;
            uint32_t v = uint32_t(rng());
            put(off, &v, 4);
        }
        }
    }
    if (size < s.size) {
        ASAN_POISON_MEMORY_REGION(s.buf + size, s.size - size);
    }
    run(s, size);
    if (size < s.size) {
        ASAN_UNPOISON_MEMORY_REGION(s.buf + size, s.size - size);
    }
    for (auto it = log.rbegin(); it != log.rend(); ++it) {
        memcpy(s.buf + it->off, it->old, it->n);
    }
}

// A category with up to three string items pointed at new strings appended
// to it (and for LC_TIME, sometimes ERA and its entry count). The strings
// are terminated, so the loader accepts them and the consumers get them.
std::string long_string(std::mt19937_64 &rng, int cat, size_t len)
{
    static const char *const conv[] = { "%c", "%Ec", "%x", "%Ex", "%X", "%EX", "%r", "%EY",
        "%EC", "%Ey", "%Oy", "%B", "%p", " ", "%%", "%", "x" };
    std::string v;
    switch (rng() % (cat == LC_TIME ? 3 : 2)) {
    case 0:
        v.assign(len, "x9 ,.%\xe2\x80\xaf"[rng() % 9]);
        break;
    case 1:
        while (v.size() < len) {
            v.push_back(char(1 + rng() % 255));
        }
        break;
    default:
        while (v.size() < len) {
            v += conv[rng() % (sizeof(conv) / sizeof(conv[0]))];
        }
    }
    return v;
}

void grow_input(std::mt19937_64 &rng)
{
    seed *s;
    do {
        s = &seeds[rng() % seeds.size()];
    } while (s->cat <= 0 || s->cat == 3);
    uint32_t n = rd(s->buf + 4);
    std::string img(s->buf, s->size);
    auto append = [&](const std::string &v) {
        while (img.size() % 4) {
            img.push_back('\0');
        }
        uint32_t off = img.size();
        img += v;
        img.push_back('\0');
        return off;
    };
    static const size_t lens[] = { 1, 16, 600, 1100, 2100, 3000, 6000 };
    const char *t = types[s->cat];
    for (int k = 1 + rng() % 3; k > 0; k--) {
        size_t len = rng() % 8 ? lens[rng() % 7] : rng() % 6000;
        if (s->cat == LC_TIME && rng() % 4 == 0) {
            // ERA (44): m entries whose formats are chains of conversions;
            // ERA_NUM_ENTRIES (50): m, or a wrong count.
            uint32_t m = 1 + rng() % 12;
            std::string era;
            for (uint32_t j = 0; j < m; j++) {
                era += (rng() % 2 ? "+:" : "-:") + std::to_string(rng() % 3) + ":" +
                    std::to_string(1900 + rng() % 200) + "/01/01:" + (rng() % 2 ? "+*" : "-*") +
                    ":E" + std::to_string(j) + ":" + long_string(rng, LC_TIME, rng() % 40);
                era.push_back('\0');
            }
            era.pop_back();
            wr(&img[8 + 4 * 44], append(era));
            uint32_t cnt = rng() % 4 ? m : uint32_t(rng());
            uint32_t w = append(std::string(4, '\0'));
            wr(&img[w], cnt);
            wr(&img[8 + 4 * 50], w);
            continue;
        }
        uint32_t i = rng() % std::min<uint32_t>(n, strlen(t));
        if (t[i] != 's') {
            continue;
        }
        wr(&img[8 + 4 * size_t(i)], append(long_string(rng, s->cat, len)));
    }
    while (img.size() % 4) {
        img.push_back('\0');
    }
    char *b = static_cast<char *>(malloc(img.size()));
    memcpy(b, img.data(), img.size());
    seed g = *s;
    g.buf = b;
    expect(run(g, img.size()), g.what + ": grown input accepted");
    free(b);
}

// End to end through osv_locale_load(): mutated files under a LOCPATH
// directory, each in a fresh locale name (the loader caches by path).
void through_files(std::mt19937_64 &rng, int count)
{
    char tmpl[] = "/tmp/fuzz-locpath-XXXXXX";
    if (!mkdtemp(tmpl)) {
        exit(2);
    }
    setenv("LOCPATH", tmpl, 1);
    unsigned long ok = 0, enoent = 0;
    for (int i = 0; i < count; i++) {
        seed &s = seeds[rng() % seeds.size()];
        if (s.cat <= 0 || s.cat == 3) {
            continue;   // small categories only; their codeset item gates loading
        }
        std::string dir = std::string(tmpl) + "/xx_YY" + std::to_string(i) + ".utf8";
        mkdir(dir.c_str(), 0755);
        mkdir((dir + "/LC_MESSAGES").c_str(), 0755);
        std::vector<char> b(s.buf, s.buf + s.size);
        size_t size = s.size;
        if (rng() % 3) {
            b[rng() % size] ^= char(1 << (rng() % 8));
        } else {
            size = rng() % size;
        }
        std::string path = dir + "/" + file_names[s.cat];
        FILE *f = fopen(path.c_str(), "wb");
        fwrite(b.data(), 1, size, f);
        fclose(f);
        errno = 0;
        std::string name = "xx_YY" + std::to_string(i) + (rng() % 2 ? ".UTF-8" : ".utf8");
        const osv_locale_data *d = osv_locale_load(s.cat, name.c_str());
        if (d) {
            read_items(*d);
            ok++;
        } else {
            expect(errno == ENOENT, "load failure is ENOENT, errno " + std::to_string(errno));
            enoent++;
        }
        unlink(path.c_str());
        rmdir((dir + "/LC_MESSAGES").c_str());
        rmdir(dir.c_str());
    }
    rmdir(tmpl);
    printf("files: %lu loaded, %lu ENOENT\n", ok, enoent);
}

// Names at and past the 255-byte limit, with codesets whose normalised form
// is longer than the name (an all-digit codeset gains "iso"), through the
// archive (LOCPATH unset; needs /usr/lib/locale/locale-archive to reach the
// archive key) and a LOCPATH directory. All must fail cleanly.
void long_names()
{
    char *saved = getenv("LOCPATH") ? strdup(getenv("LOCPATH")) : nullptr;
    for (int with_locpath = 0; with_locpath < 2; with_locpath++) {
        if (with_locpath) {
            setenv("LOCPATH", "/tmp", 1);
        } else {
            unsetenv("LOCPATH");
        }
        for (size_t len = 240; len <= 260; len++) {
            for (const char *tail : { ".1", ".12345", ".8859-1", ".utf8", ".UTF-8@x", "" }) {
                std::string n(len - strlen(tail) > 0 ? len - strlen(tail) : 1, 'a');
                n += tail;
                errno = 0;
                const osv_locale_data *d = osv_locale_load(1, n.c_str());
                expect(!d && (errno == ENOENT || errno == EINVAL),
                       "long name " + std::to_string(n.size()) + " rejected, errno " +
                       std::to_string(errno));
            }
        }
    }
    if (saved) {
        setenv("LOCPATH", saved, 1);
        free(saved);
    } else {
        unsetenv("LOCPATH");
    }
}

} // namespace

int main(int argc, char **argv)
{
    setvbuf(stdout, nullptr, _IOLBF, 0);    // a timed-out run keeps its log
    signal(SIGALRM, too_slow);
    if (argc >= 3 && !strcmp(argv[1], "--self-test")) {
        // Read every string item of a truncated file without validation:
        // must die under ASan.
        size_t size;
        char *full = read_file(argv[2], &size);
        size_t cut = 8 + 4 * size_t(rd(full + 4)) + 4;
        char *b = static_cast<char *>(malloc(cut));
        memcpy(b, full, cut);
        unsigned long acc = 0;
        for (uint32_t i = 0; i < rd(b + 4); i++) {
            acc += strlen(b + rd(b + 8 + 4 * size_t(i)));
        }
        printf("self-test: not detected (%lu)\n", acc);
        return 0;
    }
    if (argc < 4) {
        fprintf(stderr, "usage: %s <iterations> <rng-seed> <locale-dir>... [--archive <file>]\n",
                argv[0]);
        return 2;
    }
    unsigned long iters = strtoul(argv[1], nullptr, 0);
    std::mt19937_64 rng(strtoull(argv[2], nullptr, 0));
    std::vector<std::pair<std::string, std::vector<seed *>>> locs;
    std::string archive_path;
    std::vector<std::string> dirs;
    for (int i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "--archive") && i + 1 < argc) {
            archive_path = argv[++i];
        } else {
            dirs.push_back(argv[i]);
        }
    }
    seeds.reserve(13 * dirs.size() + 2);
    for (const std::string &dir : dirs) {
        std::string name = dir.substr(dir.find_last_of('/') + 1);
        locs.push_back({ name, {} });
        for (int c = 0; c < 13; c++) {
            if (c == 6) {
                continue;
            }
            seed s;
            s.what = name + "/" + file_names[c];
            s.cat = c;
            s.buf = read_file(dir + "/" + file_names[c], &s.size);
            if (!s.buf) {
                fprintf(stderr, "cannot read %s\n", s.what.c_str());
                return 2;
            }
            seeds.push_back(s);
        }
    }
    for (size_t i = 0; i < dirs.size(); i++) {
        for (size_t j = 0; j < 12; j++) {
            locs[i].second.push_back(&seeds[12 * i + j]);
        }
    }
    seeds.push_back(build_archive(locs));
    if (!archive_path.empty()) {
        seed a;
        a.what = "locale-archive";
        a.cat = -1;
        a.buf = read_file(archive_path, &a.size);
        if (!a.buf) {
            return 2;
        }
        // Its names, from its own name table (read before any mutation).
        uint32_t ho = rd(a.buf + 8), hs = rd(a.buf + 16);
        for (uint32_t i = 0; i < hs; i++) {
            uint32_t no = rd(a.buf + ho + 12 * i + 4);
            if (no) {
                a.names.push_back(a.buf + no);
            }
        }
        seeds.push_back(a);
    }
    deterministic();
    long_names();
    printf("deterministic: %lu inputs, %d failures; archive %s\n", inputs, fails,
           access("/usr/lib/locale/locale-archive", R_OK) ? "absent (long names not checked "
           "through it)" : "present");
    through_files(rng, 3000);
    unsigned long before = inputs;
    accepted = rejected = 0;
    for (unsigned long i = 0; i < iters; i++) {
        if (rng() % 8) {
            random_input(rng);
        } else {
            grow_input(rng);
        }
    }
    printf("random: %lu inputs, %lu accepted (all items read back), %lu rejected\n",
           inputs - before, accepted, rejected);
    printf("consumers: %lu LC_TIME, %lu LC_MONETARY, %lu LC_MESSAGES inputs, %lu with an item of "
           "2048 bytes or more\n", consumed[LC_TIME], consumed[LC_MONETARY], consumed[LC_MESSAGES],
           long_items);
    printf("SUMMARY: %d failures\n", fails);
    for (seed &s : seeds) {
        free(s.buf);
    }
    return fails ? 1 : 0;
}
