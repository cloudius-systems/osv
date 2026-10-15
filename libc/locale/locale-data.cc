/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Named locales from the locale data glibc's localedef writes (conf_locale=1).
// Written from the format specification (category files, the locale archive,
// codeset matching) and from observing glibc and the bytes of the files it
// compiles; no glibc source was read.
//
// A category file is uint32_t magic, nstrings, strindex[nstrings]; item i is
// at file + strindex[i] and has the type that nl_langinfo_l() shows for it
// (string, wide string, word, byte; measured, see item_types). The archive
// is a header, a name table (an open-addressing table of { hash, name
// offset, record offset }), a string pool, a locale record table and a
// checksum table; each locale record holds 13 { offset, length } category
// slices in the same file format.
//
// Search order for category cat of a locale name
// language[_territory][.codeset][@modifier], as glibc 2.41's (measured):
//   LOCPATH set and not empty: for each candidate name, each LOCPATH
//     directory, then /usr/lib/locale;
//   otherwise: the archive /usr/lib/locale/locale-archive (the name as given,
//     then with its codeset normalised), then /usr/lib/locale as above.
// The candidates, in order: with the modifier, then without it; within each,
// with the territory, then without it; within each, the codeset as given,
// normalised ("de_DE.UTF-8" also finds "de_DE.utf8"), then none. So
// "de_AT.UTF-8@euro" tries de_AT.UTF-8@euro, de_AT.utf8@euro, de_AT@euro,
// de.UTF-8@euro, de.utf8@euro, de@euro, de_AT.UTF-8, ... de. The archive is
// not searched with the shorter names (glibc neither).
// A candidate that is missing or fails validation is skipped (glibc does the
// same, per category); the first valid one decides, and if its own codeset is
// not UTF-8 the category is ENOENT (glibc too stops at a found locale whose
// codeset does not match the name). Every file is validated completely before
// any item is used: no item read leaves the data. The consumers bound what
// the strings may cost (strfmon, strftime and strptime: libc/locale/strfmon.c,
// libc/time/), and LC_CTYPE validation has a work cap.
//
// Trust: LOCPATH and the data come from the image's command line,
// environment or file system, the application's own trust (OSv has no
// setuid), so no LOCPATH restriction applies; the data is still treated as
// hostile. Installed data is taken to be immutable: a file rewritten or
// truncated while mapped can fault or change the checked offsets (out of
// scope; the alternative is reading every file into the heap).
//
// Mappings are read-only, private, and never unmapped; a loaded category is
// cached by (source, name, category) for the life of the process.

#include "locale-data.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

constexpr int ncat = 13;
constexpr int lc_all = 6;
constexpr size_t name_max = 255;
// A name with its codeset normalised: at most 3 bytes longer ("iso").
constexpr size_t norm_max = name_max + 3;

const char default_dir[] = "/usr/lib/locale";
const char archive_path[] = "/usr/lib/locale/locale-archive";

const char *const file_names[ncat] = {
    "LC_CTYPE", "LC_NUMERIC", "LC_TIME", "LC_COLLATE", "LC_MONETARY",
    "LC_MESSAGES/SYS_LC_MESSAGES", nullptr, "LC_PAPER", "LC_NAME",
    "LC_ADDRESS", "LC_TELEPHONE", "LC_MEASUREMENT", "LC_IDENTIFICATION",
};

// The type of each item of a category, by index, as measured on glibc 2.41
// by scripts/locale-oracle/item-types/ (what nl_langinfo_l() returns for
// each item of 346 compiled locales and C; the script prints these strings
// and fails if they differ): s string (NUL-terminated), w wide string
// (uint32_t, 0-terminated, aligned), d word (aligned uint32_t), b byte, r
// two words (_NL_MONETARY_CONVERSION_RATE), x internal table (only its
// offset is checked). An index past the string is x.
const char *const item_types[ncat] = {
    "xxxxxxxxxxssxdsxxdddssssssssssdwwwwwwwwwwssssssssssdddddddddddxwxwdwdxdd",
    "sssdds",
    "ssssssssssssssssssssssssssssssssssssssssssssssssssdxwwwwwwwwwwwwwwwwwwww"
    "wwwwwwwwwwwwwwwwwwwwwwwwwwwwwbdbbbbsswssssssssssssswwwwwwwwwwwwsssssssss"
    "ssswwwwwwwwwwww",
    "dxxxxxxxxxxxxdxxxxs",
    "sssssssbbbbbbbbsbbbbbbssbbbbbbbbbbbbbbddddrdds",
    "sssss",
    nullptr,
    "dds",
    "sssssss",
    "ssssssdssssss",
    "sssss",
    "bs",
    "ssssssssssssssss",
};

// The fewest items glibc 2.41 accepts in a category file (measured by
// item-types/item-min.sh): every public item, and for LC_CTYPE 86.
constexpr uint32_t min_items[ncat] = { 86, 6, 159, 19, 46, 5, 0, 3, 7, 13, 5, 2, 16 };

// The index of each category's own codeset item (_NL_CTYPE_CODESET_NAME,
// _NL_NUMERIC_CODESET, ...): the one string item that is "UTF-8" in every
// UTF-8 locale and "ISO-8859-1" in an ISO-8859-1 one (measured).
constexpr int codeset_item[ncat] = { 14, 5, 110, 18, 45, 4, -1, 2, 6, 12, 4, 1, 15 };

uint32_t magic(int cat)
{
    if (cat == 3) {
        return 0x20051014 ^ 3;
    }
    if (cat == 0) {
        return 0x20090720;
    }
    return 0x20031115 ^ uint32_t(cat);
}

uint32_t word(const char *p)
{
    uint32_t w;
    memcpy(&w, p, sizeof(w));
    return w;
}

char item_type(int cat, uint32_t idx)
{
    const char *t = item_types[cat];
    return idx < strlen(t) ? t[idx] : 'x';
}

// Is ch an ASCII letter or digit?
bool alnum(char ch)
{
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
}

bool codeset_is_utf8(const char *cs, size_t len)
{
    return osv_codeset_is_utf8(cs, len);
}

// The codeset of name (language[_territory][.codeset][@modifier]): *cs and
// *len, or false if the name has none.
bool name_codeset(const char *name, const char **cs, size_t *len)
{
    const char *dot = name;
    while (*dot && *dot != '.' && *dot != '@') {
        dot++;
    }
    if (*dot != '.') {
        return false;
    }
    const char *end = dot + 1;
    while (*end && *end != '@') {
        end++;
    }
    *cs = dot + 1;
    *len = end - (dot + 1);
    return true;
}

// name with its codeset normalised (letters lower-cased and digits kept,
// everything else dropped; "iso" prefixed if only digits remain) into out.
// Return false if the name has no codeset or normalising changes nothing.
bool normalise(const char *name, char *out, size_t outsize)
{
    const char *cs;
    size_t len;
    if (!name_codeset(name, &cs, &len)) {
        return false;
    }
    size_t pre = cs - name;
    char norm[norm_max + 1];
    size_t n = 0;
    bool letter = false;
    for (size_t i = 0; i < len; i++) {
        char ch = cs[i];
        if (alnum(ch)) {
            letter |= !(ch >= '0' && ch <= '9');
            norm[n++] = ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch;
        }
    }
    norm[n] = '\0';
    const char *rest = cs + len;
    size_t need = pre + (letter ? 0 : 3) + n + strlen(rest) + 1;
    if (need > outsize) {
        return false;
    }
    memcpy(out, name, pre);
    char *p = out + pre;
    if (!letter) {
        memcpy(p, "iso", 3);
        p += 3;
    }
    memcpy(p, norm, n);
    strcpy(p + n, rest);
    return strcmp(out, name) != 0;
}

// Is there a NUL within [p, end)?
bool terminated(const char *p, const char *end)
{
    return p < end && memchr(p, 0, end - p);
}

bool wide_terminated(const char *p, const char *end)
{
    for (; end - p >= 4; p += 4) {
        if (!word(p)) {
            return true;
        }
    }
    return false;
}

// The items of a validated category that are this locale's own codeset.
bool own_codeset_utf8(const osv_locale_data *d)
{
    const char *cs = d->base + word(d->base + 8 + 4 * codeset_item[d->cat]);
    return codeset_is_utf8(cs, strlen(cs));
}

// ---- the cache ------------------------------------------------------------

// ponytail: a linear list; a program loads a handful of locales (14 locales
// of 12 categories is 168 entries). A hash table if that ever shows up.
struct cache_entry {
    cache_entry *next;
    osv_locale_data data;
    int cat;
    char key[1];    // "<path>" or "archive:<name>"
};

pthread_mutex_t cache_mutex = PTHREAD_MUTEX_INITIALIZER;
cache_entry *cache;

const osv_locale_data *cache_find(const char *key, int cat)
{
    for (cache_entry *e = cache; e; e = e->next) {
        if (e->cat == cat && !strcmp(e->key, key)) {
            return &e->data;
        }
    }
    return nullptr;
}

void point_byte_tables(osv_locale_data *d);

// Set when cache_add() fails, so the load reports ENOMEM (under cache_mutex).
bool cache_nomem;

const osv_locale_data *cache_add(const char *key, int cat, const osv_locale_data &d)
{
    size_t len = strlen(key);
    cache_entry *e = static_cast<cache_entry *>(malloc(sizeof(cache_entry) + len));
    if (!e) {
        cache_nomem = true;
        return nullptr;
    }
    e->data = d;
    if (cat == 0) {
        point_byte_tables(&e->data);    // at the entry's copies, not d's
    }
    e->cat = cat;
    memcpy(e->key, key, len + 1);
    e->next = cache;
    cache = e;
    return &e->data;
}

// Map a whole file read-only; nullptr if it cannot be opened or is empty or
// too large for the format's 32-bit offsets. O_NONBLOCK: a FIFO named
// LC_TIME must not block the open (fstat then rejects it).
const char *map_file(const char *path, size_t *size)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) {
        return nullptr;
    }
    struct stat st;
    void *p = MAP_FAILED;
    if (!fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_size > 0 &&
        uint64_t(st.st_size) <= UINT32_MAX) {
        p = mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    }
    close(fd);
    if (p == MAP_FAILED) {
        return nullptr;
    }
    *size = st.st_size;
    return static_cast<const char *>(p);
}

// The archive, mapped on first use if it exists (every lookup validates
// what it reads). ponytail: never re-read; a program that installs locales
// into the archive after its first lookup does not see them.
const char *archive;
size_t archive_size;
bool archive_tried;

const osv_locale_data *from_archive(int cat, const char *name)
{
    if (!archive_tried) {
        archive_tried = true;
        archive = map_file(archive_path, &archive_size);
    }
    if (!archive) {
        return nullptr;
    }
    char key[sizeof("archive:") + norm_max];
    if (strlen(name) > norm_max) {
        return nullptr;
    }
    strcpy(key, "archive:");
    strcat(key, name);
    if (const osv_locale_data *d = cache_find(key, cat)) {
        return d;
    }
    osv_locale_data d;
    if (!osv_locale_archive_find(archive, archive_size, name, cat, &d)) {
        return nullptr;
    }
    return cache_add(key, cat, d);
}

const osv_locale_data *from_dir(int cat, const char *dir, size_t dirlen, const char *name)
{
    char path[PATH_MAX];
    size_t n = strlen(name), f = strlen(file_names[cat]);
    if (!dirlen || dirlen + 1 + n + 1 + f + 1 > sizeof(path)) {
        return nullptr;
    }
    memcpy(path, dir, dirlen);
    path[dirlen] = '/';
    memcpy(path + dirlen + 1, name, n);
    path[dirlen + 1 + n] = '/';
    memcpy(path + dirlen + 2 + n, file_names[cat], f + 1);
    if (const osv_locale_data *d = cache_find(path, cat)) {
        return d;
    }
    size_t size;
    const char *p = map_file(path, &size);
    if (!p) {
        return nullptr;
    }
    osv_locale_data d;
    if (!osv_locale_check(cat, p, size, &d)) {
        munmap(const_cast<char *>(p), size);
        return nullptr;
    }
    const osv_locale_data *r = cache_add(path, cat, d);
    if (!r) {
        munmap(const_cast<char *>(p), size);
    }
    return r;
}

// Each LOCPATH directory (if locpath) then the default directory.
const osv_locale_data *from_dirs(int cat, const char *locpath, const char *name)
{
    for (const char *p = locpath; p && *p;) {
        const char *colon = strchr(p, ':');
        size_t len = colon ? size_t(colon - p) : strlen(p);
        if (const osv_locale_data *d = from_dir(cat, p, len, name)) {
            return d;
        }
        p += len + (colon ? 1 : 0);
    }
    return from_dir(cat, default_dir, strlen(default_dir), name);
}

// Each candidate name of the directory search (see the top of the file),
// through every directory; the first found wins.
const osv_locale_data *from_dirs_fallback(int cat, const char *locpath, const char *name)
{
    // language ends at the first '_', '.' or '@'; territory at '.' or '@';
    // codeset at '@'; the modifier is the rest.
    size_t lang = strcspn(name, "_.@");
    size_t terr = name[lang] == '_' ? strcspn(name + lang, ".@") : 0;
    size_t cs = name[lang + terr] == '.' ? strcspn(name + lang + terr, "@") : 0;
    const char *mod = name + lang + terr + cs;
    char norm[norm_max + 1];
    bool has_norm = normalise(name, norm, sizeof(norm));
    // The normalised codeset, with its '.': norm differs from name only there.
    const char *ncs = norm + lang + terr;
    size_t ncslen = has_norm ? strlen(ncs) - strlen(mod) : 0;
    char cand[norm_max + 1];
    for (int m = *mod ? 0 : 1; m < 2; m++) {
        for (int t = terr ? 0 : 1; t < 2; t++) {
            for (int c = cs ? 0 : 2; c < 3; c++) {
                if (c == 1 && !has_norm) {
                    continue;
                }
                // ponytail: an empty language gets no shorter names.
                if (!lang && (m || t || c)) {
                    continue;
                }
                size_t n = 0;
                memcpy(cand, name, lang + (t ? 0 : terr));
                n = lang + (t ? 0 : terr);
                if (c == 0) {
                    memcpy(cand + n, name + lang + terr, cs);
                    n += cs;
                } else if (c == 1) {
                    memcpy(cand + n, ncs, ncslen);
                    n += ncslen;
                }
                if (!m) {
                    memcpy(cand + n, mod, strlen(mod));
                    n += strlen(mod);
                }
                cand[n] = '\0';
                if (const osv_locale_data *d = from_dirs(cat, locpath, cand)) {
                    return d;
                }
            }
        }
    }
    return nullptr;
}

// ---- LC_CTYPE tables ------------------------------------------------------

// The validation work of one LC_CTYPE: the classes and maps it may name, and
// the level-2 entries checked over all its tables. glibc 2.41's files have
// at most 19 classes and 5 maps (measured); the caps keep a hostile file
// from holding both locale locks for seconds.
constexpr uint32_t max_classes = 64, max_maps = 64;
constexpr uint64_t max_ctype_work = 1u << 22;

// Validate the 3-level table at offset off (unit: entry bytes of level 3;
// bits: entries are words of 32 class bits). Every offset it holds must lead
// to an array that lies within the data. The level-2 entries it may have
// (n1 * (mask2 + 1)) are taken from *work, the file's budget.
bool table_ok(const char *base, size_t size, uint32_t off, unsigned unit, uint64_t *work)
{
    if (off % 4 || size < 20 || off > size - 20) {
        return false;
    }
    const char *t = base + off;
    uint32_t s1 = word(t), n1 = word(t + 4), s2 = word(t + 8), m2 = word(t + 12),
             m3 = word(t + 16);
    if (s1 > 31 || s2 > 31 || n1 > 0x10000 || m2 > 0xffff || m3 > 0xffff ||
        uint64_t(n1) * (m2 + 1) > (1u << 22) || 20 + 4 * uint64_t(n1) > size - off) {
        return false;
    }
    if (uint64_t(n1) * (m2 + 1) > *work) {
        return false;
    }
    *work -= uint64_t(n1) * (m2 + 1);
    for (uint32_t i = 0; i < n1; i++) {
        uint32_t o2 = word(t + 20 + 4 * size_t(i));
        if (!o2) {
            continue;
        }
        if (o2 % 4 || uint64_t(off) + o2 + 4 * (uint64_t(m2) + 1) > size) {
            return false;
        }
        for (uint32_t j = 0; j <= m2; j++) {
            uint32_t o3 = word(t + o2 + 4 * size_t(j));
            if (o3 && ((unit > 1 && o3 % 4) ||
                       uint64_t(off) + o3 + unit * (uint64_t(m3) + 1) > size)) {
                return false;
            }
        }
    }
    return true;
}

// The nth name of a list (consecutive NUL-terminated strings ended by an
// empty one) starting at offset off, checked to end within the data.
uint32_t list_count(const char *base, size_t size, uint32_t off)
{
    uint32_t n = 0;
    for (size_t p = off; p < size && base[p]; n++) {
        const char *z = static_cast<const char *>(memchr(base + p, 0, size - p));
        if (!z) {
            return UINT32_MAX;
        }
        p = z - base + 1;
        if (p >= size) {
            return UINT32_MAX;
        }
    }
    return off < size ? n : UINT32_MAX;
}

const char *const std_class_names[12] = {
    "alnum", "alpha", "blank", "cntrl", "digit", "graph",
    "lower", "print", "punct", "space", "upper", "xdigit",
};

// Point the byte-table pointers at d's own copies (again after d is copied).
void point_byte_tables(osv_locale_data *d)
{
    d->ctype.class_b = d->class_copy + 128;
    d->ctype.toupper_b = d->toupper_copy + 128;
    d->ctype.tolower_b = d->tolower_copy + 128;
}

// Fill d->ctype from a validated LC_CTYPE; false if its tables are not sound.
bool ctype_tables(osv_locale_data *d)
{
    const char *b = d->base;
    size_t size = d->size;
    osv_ctype &c = d->ctype;
    memset(&c, 0, sizeof(c));
    auto item = [&](uint32_t i) { return word(b + 8 + 4 * size_t(i)); };
    // _NL_CTYPE_CLASS (0), TOUPPER (1), TOLOWER (3): 384 entries from -128.
    uint32_t cb = item(0), ub = item(1), lb = item(3);
    if (cb % 2 || cb > size || size - cb < 384 * 2 || ub % 4 || ub > size ||
        size - ub < 384 * 4 || lb % 4 || lb > size || size - lb < 384 * 4) {
        return false;
    }
    memcpy(d->class_copy, b + cb, sizeof(d->class_copy));
    memcpy(d->toupper_copy, b + ub, sizeof(d->toupper_copy));
    memcpy(d->tolower_copy, b + lb, sizeof(d->tolower_copy));
    point_byte_tables(d);
    // _NL_CTYPE_CLASS_NAMES (10), MAP_NAMES (11), WIDTH (12),
    // CLASS_OFFSET (17), MAP_OFFSET (18).
    c.class_offset = word(b + item(17));
    c.map_offset = word(b + item(18));
    c.nclass = list_count(b, size, item(10));
    c.nmap = list_count(b, size, item(11));
    if (c.nclass > max_classes || c.nmap > max_maps ||
        uint64_t(c.class_offset) + c.nclass > d->nstrings ||
        uint64_t(c.map_offset) + c.nmap > d->nstrings ||
        c.class_offset < 19 || c.map_offset < 19) {
        return false;
    }
    c.class_names = b + item(10);
    c.map_names = b + item(11);
    c.base = b;
    c.strindex = reinterpret_cast<const uint32_t *>(b + 8);
    uint64_t work = max_ctype_work;
    if (!table_ok(b, size, item(12), 1, &work)) {
        return false;
    }
    c.width = b + item(12);
    for (uint32_t k = 0; k < c.nclass; k++) {
        if (!table_ok(b, size, item(c.class_offset + k), 4, &work)) {
            return false;
        }
    }
    for (uint32_t k = 0; k < c.nmap; k++) {
        if (!table_ok(b, size, item(c.map_offset + k), 4, &work)) {
            return false;
        }
    }
    for (int k = 0; k < 12; k++) {
        c.cls[k] = osv_ctype_find(&c, std_class_names[k], false);
    }
    c.toupper = osv_ctype_find(&c, "toupper", true);
    c.tolower = osv_ctype_find(&c, "tolower", true);
    return true;
}

// ---- LC_COLLATE tables ----------------------------------------------------

// Validation of a compiled LC_COLLATE (STAGE1-FORMAT-SPEC.md section 6). It
// follows every path collate.cc's lookups can take: each first byte's entry,
// each wide table entry, each list to its closing entry, each range's indirect
// entries, each weight index's record and rules. The work is capped
// (localedef's files need under a million steps; the cap is 8 million).
struct collate_check {
    const char *b;
    uint64_t size;
    uint32_t nrules;
    uint64_t rules, wmb, emb, imb, wwc, ewc, iwc;
    uint64_t budget;

    bool spend(uint64_t n)
    {
        if (n > budget) {
            return false;
        }
        budget -= n;
        return true;
    }
    bool in(uint64_t off, uint64_t len) const
    {
        return off <= size && len <= size - off;
    }
    uint32_t w(uint64_t off) const
    {
        return word(b + off);
    }
    // The section's rules and the record of weight index idx: per level a
    // length byte (word, wide) and that many bytes (words).
    bool weight(uint32_t idx, bool wide)
    {
        uint64_t sec = idx >> 24;
        if (!spend(nrules) || !in(rules, (sec + 1) * nrules)) {
            return false;
        }
        uint64_t p = wide ? wwc + 4 * uint64_t(idx & 0xffffff) : wmb + (idx & 0xffffff);
        for (uint32_t l = 0; l < nrules; l++) {
            if (!in(p, wide ? 4 : 1)) {
                return false;
            }
            p += wide ? 4 + 4 * uint64_t(w(p)) : 1 + uint64_t(uint8_t(b[p]));
        }
        return p <= size;
    }
    // count indirect entries from first: each a weight index.
    bool indirect(bool wide, uint64_t first, uint64_t count)
    {
        uint64_t off = (wide ? iwc : imb) + 4 * first;
        if (!spend(count) || !in(off, 4 * count)) {
            return false;
        }
        for (uint64_t k = 0; k < count; k++) {
            uint32_t x = w(off + 4 * k);
            if (int32_t(x) < 0 || !weight(x, wide)) {
                return false;
            }
        }
        return true;
    }
    // Unit k (byte or word) of an entry's sequence at p.
    uint32_t unit(uint64_t p, uint64_t k, bool wide) const
    {
        return wide ? w(p + 4 * k) : uint8_t(b[p + k]);
    }
    // The list at p: entries { int32 i, length (byte, padded to 4; wide: word),
    // then length units (i >= 0) or two sequences "from" and "to" (i < 0) }
    // up to an entry with i >= 0 and length 0, which always matches. No unit
    // is 0 (a string's terminator never matches one), and a range differs
    // only in its last unit.
    bool list(uint64_t p, bool wide)
    {
        uint64_t u = wide ? 4 : 1, hdr = wide ? 8 : 5;
        for (;;) {
            if (!spend(1) || !in(p, hdr)) {
                return false;
            }
            int32_t i = int32_t(w(p));
            uint64_t len = wide ? w(p + 4) : uint8_t(b[p + 4]);
            uint64_t seqs = i >= 0 ? 1 : 2;
            if (!in(p + hdr, seqs * len * u) || !spend(seqs * len)) {
                return false;
            }
            for (uint64_t k = 0; k < seqs * len; k++) {
                if (!unit(p + hdr, k, wide)) {
                    return false;
                }
            }
            if (i >= 0) {
                if (!weight(uint32_t(i), wide)) {
                    return false;
                }
                if (!len) {
                    return true;
                }
            } else {
                if (!len) {
                    return false;
                }
                for (uint64_t k = 0; k + 1 < len; k++) {
                    if (unit(p + hdr, k, wide) != unit(p + hdr, len + k, wide)) {
                        return false;
                    }
                }
                uint32_t lo = unit(p + hdr, len - 1, wide), hi = unit(p + hdr, 2 * len - 1, wide);
                if (lo > hi || !indirect(wide, 0u - uint32_t(i), uint64_t(hi - lo) + 1)) {
                    return false;
                }
            }
            p += hdr + seqs * len * u;
            if (!wide) {
                p = emb + ((p - emb + 3) & ~uint64_t(3));
            }
        }
    }
    // A TABLEMB or TABLEWC value: a weight index, or a list's negated offset.
    bool value(uint32_t v, bool wide)
    {
        if (int32_t(v) >= 0) {
            return weight(v, wide);
        }
        uint64_t off = 0u - v;
        return wide ? list(ewc + 4 * off, true) : off % 4 == 0 && list(emb + off, false);
    }
};

// Fill d->collate from a validated LC_COLLATE; false if its tables are not
// sound. NRULES 0 (a locale compiled from C) has no tables to read.
bool collate_tables(osv_locale_data *d)
{
    const char *b = d->base;
    auto item = [&](uint32_t i) { return uint64_t(word(b + 8 + 4 * size_t(i))); };
    osv_collate &c = d->collate;
    memset(&c, 0, sizeof(c));
    // _NL_COLLATE_NRULES; localedef writes at most 32 levels (measured).
    c.nrules = word(b + item(0));
    if (c.nrules > 32) {
        return false;
    }
    if (!c.nrules) {
        return true;
    }
    collate_check k{b, d->size, c.nrules, item(1), item(3), item(4), item(5), item(10),
                    item(11), item(12), 1u << 23};
    uint64_t tmb = item(2), twc = item(9);
    // table_ok gained a work budget in the Stage 1a fix round; give the
    // LC_COLLATE wide table (a 3-level TABLEWC, same format as LC_CTYPE's)
    // the same bound so a crafted table cannot make validation superlinear.
    uint64_t twc_work = max_ctype_work;
    if (tmb % 4 || k.emb % 4 || k.imb % 4 || k.wwc % 4 || k.ewc % 4 || k.iwc % 4 ||
        !k.in(tmb, 256 * 4) || !table_ok(b, d->size, twc, 4, &twc_work)) {
        return false;
    }
    for (int i = 0; i < 256; i++) {
        if (!k.value(k.w(tmb + 4 * i), false)) {
            return false;
        }
    }
    // A code point without an entry has weight index 0.
    if (!k.weight(0, true)) {
        return false;
    }
    const char *t = b + twc;
    uint32_t n1 = word(t + 4), m2 = word(t + 12), m3 = word(t + 16);
    for (uint32_t i = 0; i < n1; i++) {
        uint32_t o2 = word(t + 20 + 4 * size_t(i));
        for (uint32_t j = 0; o2 && j <= m2; j++) {
            uint32_t o3 = word(t + o2 + 4 * size_t(j));
            if (o3 && !k.spend(uint64_t(m3) + 1)) {
                return false;
            }
            for (uint32_t q = 0; o3 && q <= m3; q++) {
                if (!k.value(word(t + o3 + 4 * size_t(q)), true)) {
                    return false;
                }
            }
        }
    }
    auto at = [&](uint64_t off) { return reinterpret_cast<const unsigned char *>(b + off); };
    c.rules = at(k.rules);
    c.table_mb = at(tmb);
    c.weight_mb = at(k.wmb);
    c.extra_mb = at(k.emb);
    c.indirect_mb = at(k.imb);
    c.table_wc = b + twc;
    c.weight_wc = at(k.wwc);
    c.extra_wc = at(k.ewc);
    c.indirect_wc = at(k.iwc);
    return true;
}

// The level-3 entry for wc of a validated table, or null.
const char *leaf(const char *t, uint32_t wc, unsigned shift3, unsigned unit)
{
    uint32_t s1 = word(t), n1 = word(t + 4), s2 = word(t + 8), m2 = word(t + 12),
             m3 = word(t + 16);
    uint32_t i1 = wc >> s1;
    if (i1 >= n1) {
        return nullptr;
    }
    uint32_t o2 = word(t + 20 + 4 * size_t(i1));
    if (!o2) {
        return nullptr;
    }
    uint32_t o3 = word(t + o2 + 4 * size_t((wc >> s2) & m2));
    if (!o3) {
        return nullptr;
    }
    return t + o3 + unit * size_t((wc >> shift3) & m3);
}

} // namespace

int osv_ctype_class(const char *t, uint32_t wc) noexcept
{
    const char *p = t ? leaf(t, wc, 5, 4) : nullptr;
    return p ? (word(p) >> (wc & 31)) & 1 : 0;
}

uint32_t osv_table_word(const char *t, uint32_t wc) noexcept
{
    const char *p = t ? leaf(t, wc, 0, 4) : nullptr;
    return p ? word(p) : 0;
}

uint32_t osv_ctype_map(const char *t, uint32_t wc) noexcept
{
    return wc + osv_table_word(t, wc);
}

int osv_ctype_width(const char *t, uint32_t wc) noexcept
{
    const char *p = t ? leaf(t, wc, 0, 1) : nullptr;
    return p && static_cast<unsigned char>(*p) != 0xff ? static_cast<unsigned char>(*p) : -1;
}

const char *osv_ctype_find(const osv_ctype *c, const char *name, bool map) noexcept
{
    const char *p = map ? c->map_names : c->class_names;
    uint32_t n = map ? c->nmap : c->nclass, first = map ? c->map_offset : c->class_offset;
    for (uint32_t k = 0; k < n; k++, p += strlen(p) + 1) {
        if (!strcmp(p, name)) {
            return c->base + c->strindex[first + k];
        }
    }
    return nullptr;
}

bool osv_locale_check(int cat, const char *base, size_t size, osv_locale_data *out) noexcept
{
    if (cat < 0 || cat >= ncat || cat == lc_all || !base || size < 8 || size > UINT32_MAX ||
        uintptr_t(base) % 4) {
        return false;
    }
    const char *end = base + size;
    if (word(base) != magic(cat)) {
        return false;
    }
    uint32_t n = word(base + 4);
    if (n < min_items[cat] || 8 + 4 * uint64_t(n) >= size) {
        return false;
    }
    for (uint32_t i = 0; i < n; i++) {
        uint32_t off = word(base + 8 + 4 * size_t(i));
        if (off > size) {
            return false;
        }
        const char *p = base + off;
        switch (item_type(cat, i)) {
        case 's':
            if (!terminated(p, end)) {
                return false;
            }
            break;
        case 'w':
            if (off % 4 || !wide_terminated(p, end)) {
                return false;
            }
            break;
        case 'd':
            if (off % 4 || size - off < 4) {
                return false;
            }
            break;
        case 'r':
            if (off % 4 || size - off < 8) {
                return false;
            }
            break;
        case 'b':
            if (off >= size) {
                return false;
            }
            break;
        }
    }
    out->base = base;
    out->size = uint32_t(size);
    out->nstrings = n;
    out->cat = cat;
    return cat == 0 ? ctype_tables(out) : cat != 3 || collate_tables(out);
}

bool osv_locale_archive_find(const char *a, size_t size, const char *name, int cat,
                             osv_locale_data *out) noexcept
{
    // magic, serial, then the name table { offset, used, size }, the string
    // pool, the locale record table and the checksum table; only the name
    // table is needed.
    if (!a || size < 14 * 4 || size > UINT32_MAX || uintptr_t(a) % 4 ||
        word(a) != 0xde020109 || cat < 0 || cat >= ncat || cat == lc_all) {
        return false;
    }
    uint32_t hash_off = word(a + 8), hash_size = word(a + 16);
    if (hash_size <= 2 || hash_off % 4 || hash_off > size ||
        uint64_t(hash_size) * 12 > size - hash_off) {
        return false;
    }
    size_t len = strlen(name);
    if (!len) {
        return false;
    }
    // The name hash, as measured: seeded with the length, each byte added
    // after rotating left by 9; it reproduces every stored hash value.
    uint32_t h = uint32_t(len);
    for (size_t i = 0; i < len; i++) {
        h = ((h << 9) | (h >> 23)) + static_cast<unsigned char>(name[i]);
    }
    uint32_t idx = h % hash_size, step = 1 + h % (hash_size - 2);
    for (uint32_t tries = 0; tries < hash_size; tries++) {
        const char *e = a + hash_off + 12 * size_t(idx);
        uint32_t name_off = word(e + 4), rec_off = word(e + 8);
        if (!name_off) {
            return false;
        }
        if (word(e) == h && name_off < size && size - name_off > len &&
            !memcmp(a + name_off, name, len + 1)) {
            if (rec_off % 4 || rec_off > size || size - rec_off < 4 + 13 * 8) {
                return false;
            }
            uint32_t off = word(a + rec_off + 4 + 8 * cat), clen = word(a + rec_off + 8 + 8 * cat);
            if (off > size || clen > size - off) {
                return false;
            }
            return osv_locale_check(cat, a + off, clen, out);
        }
        idx += step;
        if (idx >= hash_size) {
            idx -= hash_size;
        }
    }
    return false;
}

const osv_locale_data *osv_locale_load(int cat, const char *name) noexcept
{
    if (cat < 0 || cat >= ncat || cat == lc_all || !name) {
        errno = EINVAL;
        return nullptr;
    }
    size_t len = strlen(name);
    if (len > name_max || strchr(name, '/') || !strcmp(name, "..")) {
        errno = EINVAL;
        return nullptr;
    }
    // A name with a codeset that is not UTF-8 can only match a locale of
    // that codeset, and OSv loads only UTF-8 locales.
    const char *cs;
    size_t cslen;
    if (!len || (name_codeset(name, &cs, &cslen) && !codeset_is_utf8(cs, cslen))) {
        errno = ENOENT;
        return nullptr;
    }
    char norm[norm_max + 1];
    bool has_norm = normalise(name, norm, sizeof(norm));
    const char *locpath = getenv("LOCPATH");
    if (locpath && !*locpath) {
        locpath = nullptr;
    }
    const osv_locale_data *d = nullptr;
    pthread_mutex_lock(&cache_mutex);
    cache_nomem = false;
    if (!locpath) {
        d = from_archive(cat, name);
        if (!d && has_norm) {
            d = from_archive(cat, norm);
        }
    }
    if (!d) {
        d = from_dirs_fallback(cat, locpath, name);
    }
    bool nomem = !d && cache_nomem;
    pthread_mutex_unlock(&cache_mutex);
    if (!d || !own_codeset_utf8(d)) {
        errno = nomem ? ENOMEM : ENOENT;
        return nullptr;
    }
    return d;
}

char *osv_locale_item(const osv_locale_data *d, int idx) noexcept
{
    if (!d || idx < 0 || uint32_t(idx) >= d->nstrings) {
        return const_cast<char *>("");
    }
    uint32_t off = word(d->base + 8 + 4 * size_t(idx));
    if (item_type(d->cat, idx) == 'd') {
        return reinterpret_cast<char *>(uintptr_t(word(d->base + off)));
    }
    if (off >= d->size) {
        return const_cast<char *>("");
    }
    return const_cast<char *>(d->base + off);
}

const char *osv_locale_list(const osv_locale_data *d, int idx, unsigned i) noexcept
{
    if (!d || idx < 0 || uint32_t(idx) >= d->nstrings) {
        return i ? nullptr : "";
    }
    uint32_t off = word(d->base + 8 + 4 * size_t(idx));
    if (item_type(d->cat, idx) == 'd' || off >= d->size) {
        return i ? nullptr : "";
    }
    const char *p = d->base + off, *end = d->base + d->size;
    for (;; i--) {
        auto z = static_cast<const char *>(memchr(p, 0, size_t(end - p)));
        if (!z) {
            return nullptr;
        }
        if (!i) {
            return p;
        }
        p = z + 1;
        if (p >= end) {
            return nullptr;
        }
    }
}
