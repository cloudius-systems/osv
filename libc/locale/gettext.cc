/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// gettext and its family with glibc 2.41's observable rules, measured by
// running it (scripts/locale-oracle/messages/rules*.sh; no glibc or GNU
// gettext source was read). The .mo format and plural expressions are in
// mo.cc. Built with conf_locale=1 only: with conf_locale=0 no locale but C
// and C.UTF-8 can be selected, under which nothing translates, so the intl.c
// stub (msgid, no file access) is already exact there.
//
// - A message of domain d (default "messages") and category c is looked up
//   in <dir>/<name>/<LC_c>/<d>.mo, dir from bindtextdomain (default
//   /usr/share/locale), for each name of a language list and each of the
//   name's fallback candidates (osv_locale_candidates: the locale lookup's
//   order); the first catalog that has the message wins.
// - The language list: none if c's locale (the thread's, or strerror_l's) is
//   C-like ("C", "POSIX", "C.<x>"); else LANGUAGE's colon-separated entries
//   (empty ones skipped; an entry "C", "POSIX" or "C.<x>" ends the list; an
//   entry found in locale.alias, case-insensitively, is replaced by its
//   value) if LANGUAGE is not empty, else c's locale name.
// - A catalog that cannot be read, is not a valid .mo or has a charset this
//   code does not convert is skipped. A translation is converted to the
//   codeset bound with bind_textdomain_codeset, else to LC_CTYPE's: UTF-8, or
//   in a C LC_CTYPE ASCII with glibc's C transliteration (ctype-utf8.cc).
//   Catalog charsets: UTF-8, ISO-8859-1, ASCII, or none given (bytes as
//   they are).
// - Plural forms from the header's nplurals= and plural=, else n != 1; an
//   index past the forms present is form 0. errno is preserved.
//
// Not as glibc: a bound codeset that is not UTF-8 returns the msgid (glibc
// converts; Stage 1d converts to UTF-8 only); glibc remembers translations it
// found and keeps them after LANGUAGE changes until textdomain(), setlocale()
// or a binding changes, OSv reads LANGUAGE on every call; a plural expression
// dividing by zero gives the default form (glibc raises SIGFPE).
//
// Catalogs are read once into private read-only memory (not mapped from the
// file: OSv's kernel calls strerror() too, possibly after vfs_exit() has
// unmounted the file system) and never freed; so are the caches.

#include "intl.h"
#include "mo.h"
#include "locale-data.h"

#include <atomic>
#include <errno.h>
#include <fcntl.h>
#include <libintl.h>
#include <locale.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wchar.h>

#ifdef __OSV__
#include <osv/export.h>
#else
#define OSV_LIBC_API
#endif

extern "C" __attribute__((visibility("hidden")))
bool __osv_c_translit(wchar_t wc, const char **out, size_t *len);

namespace {

constexpr int ncat = 13;
constexpr int lc_all = 6;
const char default_dir[] = "/usr/share/locale";
const char default_domain[] = "messages";
const char alias_path[] = "/usr/share/locale/locale.alias";
const char *const cat_dirs[ncat] = {
    "LC_CTYPE", "LC_NUMERIC", "LC_TIME", "LC_COLLATE", "LC_MONETARY",
    "LC_MESSAGES", nullptr, "LC_PAPER", "LC_NAME", "LC_ADDRESS",
    "LC_TELEPHONE", "LC_MEASUREMENT", "LC_IDENTIFICATION",
};

pthread_mutex_t intl_mutex = PTHREAD_MUTEX_INITIALIZER;

struct lock_guard {
    lock_guard() { pthread_mutex_lock(&intl_mutex); }
    ~lock_guard() { pthread_mutex_unlock(&intl_mutex); }
};

char *dup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = static_cast<char *>(malloc(n));
    if (p) {
        memcpy(p, s, n);
    }
    return p;
}

// ---- textdomain, bindings ---------------------------------------------------

// Replaced strings are not freed: a reader may hold them.
std::atomic<const char *> current_domain{default_domain};

struct binding {
    binding *next;
    const char *domain;
    std::atomic<const char *> dir, codeset;
};
std::atomic<binding *> bindings{nullptr};

binding *find_binding(const char *domain)
{
    for (binding *b = bindings.load(std::memory_order_acquire); b; b = b->next) {
        if (!strcmp(b->domain, domain)) {
            return b;
        }
    }
    return nullptr;
}

// Set (value non-null) or query a binding's dir (which 0) or codeset (1).
char *bind(const char *domain, const char *value, int which)
{
    if (!domain || !*domain) {
        return nullptr;
    }
    binding *b = find_binding(domain);
    if (!value) {
        if (!b) {
            return const_cast<char *>(which ? nullptr : default_dir);
        }
        const char *v = (which ? b->codeset : b->dir).load(std::memory_order_acquire);
        return const_cast<char *>(v ? v : which ? nullptr : default_dir);
    }
    lock_guard g;
    b = find_binding(domain);
    if (!b) {
        b = static_cast<binding *>(calloc(1, sizeof(binding)));
        char *d = dup(domain);
        if (!b || !d) {
            free(b);
            free(d);
            return nullptr;
        }
        b->domain = d;
        b->next = bindings.load(std::memory_order_relaxed);
        bindings.store(b, std::memory_order_release);
    }
    char *v = dup(value);
    if (!v) {
        return nullptr;
    }
    (which ? b->codeset : b->dir).store(v, std::memory_order_release);
    return v;
}

// ---- files ------------------------------------------------------------------

// The whole file in private read-only memory, or nullptr.
const char *read_file(const char *path, size_t *size)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return nullptr;
    }
    struct stat st;
    void *p = MAP_FAILED;
    if (!fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_size > 0 &&
        uint64_t(st.st_size) <= UINT32_MAX) {
        p = mmap(nullptr, st.st_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    }
    size_t got = 0;
    while (p != MAP_FAILED && got < size_t(st.st_size)) {
        ssize_t r = read(fd, static_cast<char *>(p) + got, st.st_size - got);
        if (r < 0 && errno == EINTR) {
            continue;
        }
        if (r <= 0) {
            munmap(p, st.st_size);
            p = MAP_FAILED;
            break;
        }
        got += r;
    }
    close(fd);
    if (p == MAP_FAILED) {
        return nullptr;
    }
    mprotect(p, st.st_size, PROT_READ);
    *size = st.st_size;
    return static_cast<const char *>(p);
}

// locale.alias, read on first use. ponytail: never re-read.
const char *alias_data;
size_t alias_size;
std::atomic<bool> alias_read{false};

// The value of name in locale.alias (case-insensitive key) copied to out.
bool alias(const char *name, size_t len, char *out, size_t outsize)
{
    if (!alias_read.load(std::memory_order_acquire)) {
        lock_guard g;
        if (!alias_read.load(std::memory_order_relaxed)) {
            alias_data = read_file(alias_path, &alias_size);
            alias_read.store(true, std::memory_order_release);
        }
    }
    const char *p = alias_data, *end = alias_data + alias_size;
    while (p && p < end) {
        const char *nl = static_cast<const char *>(memchr(p, '\n', end - p));
        const char *le = nl ? nl : end;
        const char *k = p;
        while (k < le && (*k == ' ' || *k == '\t')) {
            k++;
        }
        const char *ke = k;
        while (ke < le && *ke != ' ' && *ke != '\t') {
            ke++;
        }
        if (k < le && *k != '#' && size_t(ke - k) == len && !strncasecmp(k, name, len)) {
            const char *v = ke;
            while (v < le && (*v == ' ' || *v == '\t')) {
                v++;
            }
            const char *ve = v;
            while (ve < le && *ve != ' ' && *ve != '\t' && *ve != '\r') {
                ve++;
            }
            if (v < ve && size_t(ve - v) < outsize) {
                memcpy(out, v, ve - v);
                out[ve - v] = '\0';
                return true;
            }
            return false;
        }
        p = le + 1;
    }
    return false;
}

// ---- catalogs ---------------------------------------------------------------

enum charset { cs_asis, cs_utf8, cs_latin1, cs_ascii, cs_unknown };
enum target { to_utf8, to_ascii, ntargets };

// Uppercase letters and digits of a codeset name: "utf-8" -> "UTF8".
void norm_cs(const char *s, size_t len, char *out, size_t outsize)
{
    size_t n = 0;
    for (size_t i = 0; i < len && n + 1 < outsize; i++) {
        char c = s[i];
        if (c >= 'a' && c <= 'z') {
            out[n++] = c - 'a' + 'A';
        } else if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            out[n++] = c;
        }
    }
    out[n] = '\0';
}

charset classify(const char *name, size_t len)
{
    char n[32];
    if (!len) {
        return cs_asis;
    }
    if (len >= sizeof(n)) {
        return cs_unknown;
    }
    norm_cs(name, len, n, sizeof(n));
    // ponytail: the common spellings only; an unknown name skips the catalog.
    static const char *const utf8[] = { "UTF8", nullptr };
    static const char *const latin1[] = { "ISO88591", "LATIN1", "L1", "ISO885911987",
                                          "ISOIR100", "CP819", "IBM819", nullptr };
    static const char *const ascii[] = { "ASCII", "ANSIX341968", "USASCII", "ISO646US",
                                         "US", "CP367", "IBM367", nullptr };
    const char *const *sets[] = { utf8, latin1, ascii };
    const charset kinds[] = { cs_utf8, cs_latin1, cs_ascii };
    for (int s = 0; s < 3; s++) {
        for (const char *const *p = sets[s]; *p; p++) {
            if (!strcmp(n, *p)) {
                return kinds[s];
            }
        }
    }
    return cs_unknown;
}

// A converted translation: its length, then its bytes and a NUL.
struct converted {
    uint32_t len;
    char s[1];
};

struct catalog {
    osv_mo mo;
    osv_plural plural;
    charset cs;
    std::atomic<std::atomic<converted *> *> conv[ntargets];
};

// The catalog's charset from its header's "charset=".
charset header_charset(const char *h)
{
    const char *p = h ? strstr(h, "charset=") : nullptr;
    if (!p) {
        return cs_asis;
    }
    p += 8;
    size_t len = strcspn(p, " \t\n;");
    return classify(p, len);
}

struct path_entry {
    path_entry *next;
    catalog *cat; // null: no usable catalog there
    char path[1];
};
std::atomic<path_entry *> paths{nullptr};

catalog *load(const char *path)
{
    size_t size;
    const char *base = read_file(path, &size);
    if (!base) {
        return nullptr;
    }
    catalog *c = static_cast<catalog *>(calloc(1, sizeof(catalog)));
    if (!c || !osv_mo_open(base, size, &c->mo)) {
        free(c);
        munmap(const_cast<char *>(base), size);
        return nullptr;
    }
    size_t hl;
    uint32_t idx;
    const char *header = osv_mo_find(&c->mo, "", &hl, &idx);
    c->cs = header_charset(header);
    if (c->cs == cs_unknown) {
        free(c);
        munmap(const_cast<char *>(base), size);
        return nullptr;
    }
    osv_plural_from_header(header, &c->plural);
    return c;
}

// The catalog at path, loaded on first use; null if there is none.
// ponytail: a linear list of every path tried, never re-read (a catalog
// installed later, or a relative path after chdir(), is not seen); a hash
// map if programs try many.
catalog *find_catalog(const char *path)
{
    for (path_entry *e = paths.load(std::memory_order_acquire); e; e = e->next) {
        if (!strcmp(e->path, path)) {
            return e->cat;
        }
    }
    lock_guard g;
    for (path_entry *e = paths.load(std::memory_order_relaxed); e; e = e->next) {
        if (!strcmp(e->path, path)) {
            return e->cat;
        }
    }
    size_t len = strlen(path);
    path_entry *e = static_cast<path_entry *>(malloc(sizeof(path_entry) + len));
    if (!e) {
        return nullptr;
    }
    memcpy(e->path, path, len + 1);
    e->cat = load(path);
    e->next = paths.load(std::memory_order_relaxed);
    paths.store(e, std::memory_order_release);
    return e->cat;
}

// ---- conversion -------------------------------------------------------------

// Decode one character of a strict UTF-8 sequence; 0 bytes on error.
size_t utf8_decode(const unsigned char *s, size_t n, uint32_t *wc)
{
    unsigned char c = s[0];
    size_t len;
    uint32_t v, min;
    if (c < 0x80) {
        *wc = c;
        return 1;
    } else if (c >= 0xc2 && c <= 0xdf) {
        len = 2; v = c & 0x1f; min = 0x80;
    } else if (c >= 0xe0 && c <= 0xef) {
        len = 3; v = c & 0x0f; min = 0x800;
    } else if (c >= 0xf0 && c <= 0xf4) {
        len = 4; v = c & 0x07; min = 0x10000;
    } else {
        return 0;
    }
    if (len > n) {
        return 0;
    }
    for (size_t i = 1; i < len; i++) {
        if ((s[i] & 0xc0) != 0x80) {
            return 0;
        }
        v = v << 6 | (s[i] & 0x3f);
    }
    if (v < min || v > 0x10ffff || (v >= 0xd800 && v <= 0xdfff)) {
        return 0;
    }
    *wc = v;
    return len;
}

// Append wc in the target codeset (ASCII: glibc's C transliteration, else '?').
void put(target t, uint32_t wc, char *out, size_t *n)
{
    if (t == to_utf8) {
        if (wc < 0x80) {
            out[(*n)++] = wc;
        } else if (wc < 0x800) {
            out[(*n)++] = 0xc0 | wc >> 6;
            out[(*n)++] = 0x80 | (wc & 0x3f);
        } else if (wc < 0x10000) {
            out[(*n)++] = 0xe0 | wc >> 12;
            out[(*n)++] = 0x80 | (wc >> 6 & 0x3f);
            out[(*n)++] = 0x80 | (wc & 0x3f);
        } else {
            out[(*n)++] = 0xf0 | wc >> 18;
            out[(*n)++] = 0x80 | (wc >> 12 & 0x3f);
            out[(*n)++] = 0x80 | (wc >> 6 & 0x3f);
            out[(*n)++] = 0x80 | (wc & 0x3f);
        }
        return;
    }
    if (wc < 0x80) {
        out[(*n)++] = wc;
        return;
    }
    const char *s;
    size_t l;
    if (!__osv_c_translit(wchar_t(wc), &s, &l)) {
        out[(*n)++] = '?';
        return;
    }
    memcpy(out + *n, s, l);
    *n += l;
}

// The longest transliteration in ctype-utf8.cc's table is well under this.
constexpr size_t max_out_per_char = 16;

// Translation i of c (len bytes at s, forms separated by NULs) in target t:
// the cached conversion, or nullptr if it cannot be converted.
const converted *convert(catalog *c, uint32_t i, const char *s, size_t len, target t)
{
    std::atomic<converted *> *a = c->conv[t].load(std::memory_order_acquire);
    if (!a) {
        a = static_cast<std::atomic<converted *> *>(
            calloc(c->mo.n ? c->mo.n : 1, sizeof(std::atomic<converted *>)));
        if (!a) {
            return nullptr;
        }
        std::atomic<converted *> *expected = nullptr;
        if (!c->conv[t].compare_exchange_strong(expected, a, std::memory_order_acq_rel)) {
            free(a);
            a = expected;
        }
    }
    std::atomic<converted *> &slot = a[i];
    if (converted *v = slot.load(std::memory_order_acquire)) {
        return v;
    }
    if (len > (UINT32_MAX - sizeof(converted)) / max_out_per_char) {
        return nullptr;
    }
    converted *v = static_cast<converted *>(malloc(sizeof(converted) + len * max_out_per_char));
    if (!v) {
        return nullptr;
    }
    size_t n = 0;
    const unsigned char *p = reinterpret_cast<const unsigned char *>(s);
    for (size_t k = 0; k < len;) {
        uint32_t wc;
        size_t used = 1;
        if (c->cs == cs_latin1) {
            wc = p[k];
        } else if (c->cs == cs_ascii) {
            if (p[k] >= 0x80) {
                free(v);
                return nullptr;
            }
            wc = p[k];
        } else if (!(used = utf8_decode(p + k, len - k, &wc))) {
            free(v);
            return nullptr;
        }
        put(t, wc, v->s, &n);
        k += used;
    }
    v->s[n] = '\0';
    v->len = n;
    converted *expected = nullptr;
    if (!slot.compare_exchange_strong(expected, v, std::memory_order_acq_rel)) {
        free(v);
        return expected;
    }
    return v;
}

// ---- lookup -----------------------------------------------------------------

bool c_like(const char *name)
{
    return !strcmp(name, "C") || !strcmp(name, "POSIX") || !strncmp(name, "C.", 2);
}

struct search {
    const char *dir, *catdir, *domain, *msgid;
    size_t dirlen, catlen, domlen;
    catalog *found;
    const char *trans;
    size_t len;
    uint32_t index;
};

bool try_candidate(const char *cand, void *arg)
{
    search *s = static_cast<search *>(arg);
    size_t cl = strlen(cand);
    size_t need = s->dirlen + 1 + cl + 1 + s->catlen + 1 + s->domlen + 4;
    if (need > 4096) {
        return false;
    }
    char path[need];
    char *p = path;
    memcpy(p, s->dir, s->dirlen); p += s->dirlen;
    *p++ = '/';
    memcpy(p, cand, cl); p += cl;
    *p++ = '/';
    memcpy(p, s->catdir, s->catlen); p += s->catlen;
    *p++ = '/';
    memcpy(p, s->domain, s->domlen); p += s->domlen;
    memcpy(p, ".mo", 4);
    catalog *c = find_catalog(path);
    if (!c) {
        return false;
    }
    s->trans = osv_mo_find(&c->mo, s->msgid, &s->len, &s->index);
    if (!s->trans) {
        return false;
    }
    s->found = c;
    return true;
}

// Search language (len bytes) and its fallback candidates.
bool try_language(search *s, const char *lang, size_t len, bool use_alias)
{
    char name[256], value[256];
    if (len >= sizeof(name)) {
        return false;
    }
    memcpy(name, lang, len);
    name[len] = '\0';
    const char *n = use_alias && alias(name, len, value, sizeof(value)) ? value : name;
    // A name without a language part ("../x/de", ".utf8") has no candidates
    // in the locale lookup; glibc's gettext still tries it as it is.
    if (!strcspn(n, "_.@") && try_candidate(n, s)) {
        return true;
    }
    return osv_locale_candidates(n, try_candidate, s);
}

const char *lookup(const char *domain, const char *msgid1, const char *msgid2,
                   unsigned long n, int cat, locale_t l)
{
    const char *untranslated = msgid2 && n != 1 ? msgid2 : msgid1;
    if (cat < 0 || cat >= ncat || cat == lc_all || !msgid1) {
        return untranslated;
    }
    const char *locname = __osv_locale_name(cat, l);
    if (!locname || c_like(locname)) {
        return untranslated;
    }
    if (!domain) {
        domain = current_domain.load(std::memory_order_acquire);
    }
    binding *b = find_binding(domain);
    const char *dir = b ? b->dir.load(std::memory_order_acquire) : nullptr;
    const char *bcs = b ? b->codeset.load(std::memory_order_acquire) : nullptr;
    target t;
    if (bcs && *bcs) {
        if (classify(bcs, strlen(bcs)) != cs_utf8) {
            return untranslated;
        }
        t = to_utf8;
    } else {
        t = __osv_ctype_utf8(l) ? to_utf8 : to_ascii;
    }
    search s{};
    s.dir = dir ? dir : default_dir;
    s.dirlen = strlen(s.dir);
    s.catdir = cat_dirs[cat];
    s.catlen = strlen(s.catdir);
    s.domain = domain;
    s.domlen = strlen(domain);
    s.msgid = msgid1;
    const char *language = getenv("LANGUAGE");
    bool found = false;
    if (language && *language) {
        for (const char *p = language; *p && !found;) {
            size_t len = strcspn(p, ":");
            char entry[256];
            if (len && len < sizeof(entry)) {
                memcpy(entry, p, len);
                entry[len] = '\0';
                if (c_like(entry)) {
                    break;
                }
                found = try_language(&s, p, len, true);
            }
            p += len + (p[len] == ':');
        }
    } else {
        found = try_language(&s, locname, strlen(locname), false);
    }
    if (!found) {
        return untranslated;
    }
    const char *tr = s.trans;
    size_t len = s.len;
    bool asis = s.found->cs == cs_asis ||
        (s.found->cs == cs_utf8 && t == to_utf8) || (s.found->cs == cs_ascii && t == to_ascii);
    if (!asis) {
        const converted *v = convert(s.found, s.index, tr, len, t);
        if (!v) {
            return untranslated;
        }
        tr = v->s;
        len = v->len;
    }
    if (msgid2) {
        // Form i starts after the i-th NUL; past the forms present: form 0.
        unsigned long form = osv_plural_index(&s.found->plural, n);
        const char *p = tr, *end = tr + len;
        for (; form && p <= end; form--) {
            p += strlen(p) + 1;
        }
        if (p <= end) {
            tr = p;
        }
    }
    return tr;
}

} // namespace

extern "C" OSV_INTL_HIDDEN
const char *__osv_dcngettext_l(const char *domain, const char *msgid1, const char *msgid2,
                               unsigned long n, int cat, locale_t l) noexcept
{
    int saved = errno;
    const char *r = lookup(domain, msgid1, msgid2, n, cat, l);
    errno = saved;
    return r;
}

extern "C" OSV_LIBC_API
char *dcngettext(const char *domain, const char *msgid1, const char *msgid2,
                 unsigned long n, int cat) noexcept
{
    return const_cast<char *>(__osv_dcngettext_l(domain, msgid1, msgid2, n, cat, nullptr));
}

extern "C" OSV_LIBC_API
char *dngettext(const char *domain, const char *msgid1, const char *msgid2,
                unsigned long n) noexcept
{
    return dcngettext(domain, msgid1, msgid2, n, LC_MESSAGES);
}

extern "C" OSV_LIBC_API
char *ngettext(const char *msgid1, const char *msgid2, unsigned long n) noexcept
{
    return dcngettext(nullptr, msgid1, msgid2, n, LC_MESSAGES);
}

extern "C" OSV_LIBC_API
char *dcgettext(const char *domain, const char *msgid, int cat) noexcept
{
    return dcngettext(domain, msgid, nullptr, 1, cat);
}

extern "C" OSV_LIBC_API
char *dgettext(const char *domain, const char *msgid) noexcept
{
    return dcngettext(domain, msgid, nullptr, 1, LC_MESSAGES);
}

extern "C" OSV_LIBC_API
char *gettext(const char *msgid) noexcept
{
    return dcngettext(nullptr, msgid, nullptr, 1, LC_MESSAGES);
}

extern "C" OSV_LIBC_API
char *textdomain(const char *domain) noexcept
{
    if (!domain) {
        return const_cast<char *>(current_domain.load(std::memory_order_acquire));
    }
    if (!*domain) {
        current_domain.store(default_domain, std::memory_order_release);
        return const_cast<char *>(default_domain);
    }
    char *d = dup(domain);
    if (!d) {
        return nullptr;
    }
    current_domain.store(d, std::memory_order_release);
    return d;
}

extern "C" OSV_LIBC_API
char *bindtextdomain(const char *domain, const char *dir) noexcept
{
    return bind(domain, dir, 0);
}

extern "C" OSV_LIBC_API
char *bind_textdomain_codeset(const char *domain, const char *codeset) noexcept
{
    return bind(domain, codeset, 1);
}
