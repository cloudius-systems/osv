/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// gettext and libc's own messages (OSv conf_locale=1; with 0 the stub
// returns every msgid untranslated).
//
// Every expectation was taken from glibc 2.41 (Debian 13) and the test
// passes there too: build it on Linux with g++ -std=gnu++17 and run it as
//   OSV_LOCALE_TEST_DATA=<out of make-test-locales.sh> ./tst-locale-messages
// (the libc.mo checks then use the host's /usr/share/locale, and are SKIP
// without its de, fr and ja libc.mo). On OSv, modules/tests/Makefile installs
// the test locales under /usr/share/osv-locale-test, the test catalogs that
// make-test-locales.sh writes with msgfmt under .../msg, and Debian's de, fr
// and ja libc.mo under /usr/share/locale when the build host has them.

#include <cerrno>
#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <libintl.h>
#include <locale.h>
#include <netdb.h>
#include <regex.h>
#include <signal.h>
#include <sys/stat.h>

#ifndef OSV_CONF_LOCALE
#define OSV_CONF_LOCALE 1   // glibc
#endif

static int tests, fails, skips;

static void report(bool ok, const std::string &what)
{
    tests++;
    if (!ok) {
        fails++;
    }
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what.c_str());
}

static void skip(const std::string &what)
{
    skips++;
    printf("SKIP: %s\n", what.c_str());
}

static bool exists(const std::string &p)
{
    struct stat st;
    return !stat(p.c_str(), &st);
}

static std::string data_dir, cats;

static void eq(const char *got, const char *want, const std::string &what)
{
    bool ok = got && !strcmp(got, want);
    report(ok, what + " = \"" + (got ? got : "(null)") + "\"" + (ok ? "" : std::string(" want \"") + want + "\""));
}

static void reset()
{
    unsetenv("LANGUAGE");
    setlocale(LC_ALL, "C");
}

// The test catalogs (make-test-locales.sh, msg/): t1 with Plural-Forms
// nplurals=2 (de) or 3 (ru), contexts and 300 extra keys, a hash table;
// t1n the same without a hash table; t4 big-endian; t2 no Plural-Forms; t3
// ISO-8859-1; where.mo under each candidate name, translating "where" to it.
static void test_catalogs()
{
    reset();
    setenv("LOCPATH", (data_dir + "/dir").c_str(), 1);
    eq(bindtextdomain("t1", cats.c_str()), cats.c_str(), "bindtextdomain t1");
    for (const char *d : { "t1n", "t2", "t3", "t4", "where" }) {
        bindtextdomain(d, cats.c_str());
    }
    eq(dgettext("t1", "hello"), "hello", "C locale: untranslated");
    setenv("LANGUAGE", "de", 1);
    eq(dgettext("t1", "hello"), "hello", "C locale ignores LANGUAGE");
    unsetenv("LANGUAGE");
    report(setlocale(LC_ALL, "de_DE.UTF-8") != nullptr, "setlocale de_DE.UTF-8");
    eq(dgettext("t1", "hello"), "de:hallo", "de: dgettext");
    eq(dgettext("t1n", "k299"), "de:v299", "de: no hash table");
    eq(dgettext("t4", "k150"), "de:v150", "de: big-endian");
    eq(dgettext("t1", "k300"), "k300", "de: missing entry");
    eq(dgettext("t1", "menu\004Open"), "de:menu-oeffnen", "de: msgctxt menu");
    eq(dgettext("t1", "verb\004Open"), "de:verb-oeffnen", "de: msgctxt verb");
    eq(dgettext("t1", "none\004Open"), "none\004Open", "de: unknown context");
    eq(dgettext("t1", "Open"), "de:oeffnen", "de: no context");
    eq(dgettext("t1", "Gr\303\274\303\237e"), "de:Gr\303\274\303\237e \342\202\254", "de: UTF-8 translation");
    eq(dngettext("t1", "file", "files", 1), "de:datei0", "de: ngettext 1");
    eq(dngettext("t1", "file", "files", 2), "de:datei1", "de: ngettext 2");
    eq(dngettext("t1", "nofile", "nofiles", 1), "nofile", "de: ngettext untranslated 1");
    eq(dngettext("t1", "nofile", "nofiles", 7), "nofiles", "de: ngettext untranslated 7");
    eq(dngettext("t2", "file", "files", 0), "de:d1", "de: default plural rule n=0");
    eq(dngettext("t2", "file", "files", 1), "de:d0", "de: default plural rule n=1");
    eq(dngettext("t1", "hello", "hellos", 5), "de:hallo", "de: ngettext of a singular entry");
    eq(dgettext("t3", "Gruesse"), "de:Gr\303\274\303\237e", "de: ISO-8859-1 catalog to UTF-8");
    eq(dcgettext("t1", "hello", LC_TIME), "hello", "de: dcgettext LC_TIME (no catalog there)");
    eq(dcgettext("t1", "hello", LC_ALL), "hello", "de: dcgettext LC_ALL");
    errno = 1234;
    dgettext("t1", "hello");
    dgettext("t1", "nope");
    report(errno == 1234, "errno preserved");

    setlocale(LC_ALL, "ru_RU.UTF-8");
    const char *ru[] = { "ru:datei2", "ru:datei0", "ru:datei1", "ru:datei2" };
    unsigned long ns[] = { 0, 1, 2, 5 };
    for (int i = 0; i < 4; i++) {
        eq(dngettext("t1", "file", "files", ns[i]), ru[i], "ru: plural n=" + std::to_string(ns[i]));
    }
    eq(dngettext("t1", "file", "files", 21), "ru:datei0", "ru: plural n=21");
    eq(dngettext("t1", "file", "files", 111), "ru:datei2", "ru: plural n=111");

    // LANGUAGE: a list, per message; "C" ends it; ignored in a C locale.
    // Each check uses a msgid not translated before: glibc keeps a found
    // translation after LANGUAGE changes (OSv does not; both pass).
    setlocale(LC_ALL, "fr_FR.UTF-8");
    eq(dgettext("t1", "hello"), "hello", "fr (no catalog)");
    setenv("LANGUAGE", "xx:ru:de", 1);
    eq(dgettext("t1", "hello"), "ru:hallo", "LANGUAGE=xx:ru:de");
    setenv("LANGUAGE", "de:ru", 1);
    eq(dgettext("t2", "file"), "de:d0", "LANGUAGE=de:ru t2");
    setenv("LANGUAGE", "C:de", 1);
    eq(dgettext("t1", "k1"), "k1", "LANGUAGE=C:de");
    setenv("LANGUAGE", "german", 1);
    std::string alias = "/usr/share/locale/locale.alias";
    if (exists(alias)) {
        eq(dgettext("t1", "k2"), "de:v2", "LANGUAGE=german (locale.alias)");
    } else {
        skip("no " + alias);
    }
    setenv("LANGUAGE", "de_AT.UTF-8@euro", 1);
    eq(dgettext("where", "where"), "de_AT.UTF-8@euro", "where.mo: longest name first");
    unsetenv("LANGUAGE");

    // The catalog search order, from the LC_MESSAGES name: de_AT has no
    // locale data, so it goes through LANGUAGE; each name is tried in turn.
    // (order.sh measured it by removing the winner; here: the first one.)
    setlocale(LC_ALL, "C");
    setlocale(LC_MESSAGES, "de_DE.UTF-8");
    eq(dgettext("t1", "Gr\303\274\303\237e"), "de:Gr?sse EUR", "C LC_CTYPE: ASCII transliteration");
    setlocale(LC_CTYPE, "de_DE.UTF-8");
    eq(dgettext("t1", "Gr\303\274\303\237e"), "de:Gr\303\274\303\237e \342\202\254", "UTF-8 LC_CTYPE");
    setlocale(LC_CTYPE, "C");
    bind_textdomain_codeset("t1", "UTF-8");
    eq(dgettext("t1", "Gr\303\274\303\237e"), "de:Gr\303\274\303\237e \342\202\254", "bound UTF-8 in C LC_CTYPE");
    eq(bind_textdomain_codeset("t1", nullptr), "UTF-8", "bind_textdomain_codeset query");

    // uselocale: the thread's locale decides.
    locale_t ru_l = newlocale(LC_ALL_MASK, "ru_RU.UTF-8", nullptr);
    setlocale(LC_ALL, "de_DE.UTF-8");
    locale_t old = uselocale(ru_l);
    eq(dgettext("t1", "hello"), "ru:hallo", "uselocale ru");
    std::string other;
    std::thread th([&] { other = dgettext("t1", "hello"); });
    th.join();
    eq(other.c_str(), "de:hallo", "another thread: global de");
    uselocale(old);
    freelocale(ru_l);
    eq(dgettext("t1", "hello"), "de:hallo", "back to global de");

    // textdomain and bindtextdomain queries.
    eq(textdomain(nullptr), "messages", "textdomain(NULL)");
    eq(textdomain("t1"), "t1", "textdomain t1");
    eq(gettext("hello"), "de:hallo", "gettext in t1");
    eq(ngettext("file", "files", 3), "de:datei1", "ngettext in t1");
    eq(textdomain(""), "messages", "textdomain(\"\")");
    eq(bindtextdomain("nobody", nullptr), "/usr/share/locale", "bindtextdomain query default");
    report(bindtextdomain("", "/x") == nullptr, "bindtextdomain(\"\") NULL");

    // Many threads at once on a fresh domain (catalog loading, conversion).
    bindtextdomain("t1n", cats.c_str());
    setlocale(LC_CTYPE, "C");
    std::vector<std::thread> ts;
    std::vector<int> bad(16);
    for (int i = 0; i < 16; i++) {
        ts.emplace_back([i, &bad] {
            for (int k = 0; k < 300; k++) {
                std::string want = "de:v" + std::to_string(k);
                const char *r = dgettext("t1n", ("k" + std::to_string(k)).c_str());
                bad[i] += strcmp(r, want.c_str()) != 0;
                bad[i] += strcmp(dgettext("t1n", "Gr\303\274\303\237e"), "de:Gr?sse EUR") != 0;
            }
        });
    }
    for (auto &t : ts) {
        t.join();
    }
    int nbad = 0;
    for (int b : bad) {
        nbad += b;
    }
    report(nbad == 0, "16 threads x 300 lookups: " + std::to_string(nbad) + " wrong");
    reset();
}

// libc's messages with Debian's libc.mo (the locales package).
static void test_libc()
{
    reset();
    setenv("LOCPATH", (data_dir + "/dir").c_str(), 1);
    eq(strerror(ENOENT), "No such file or directory", "C strerror(ENOENT)");
    eq(strerror(0), "Success", "C strerror(0)");
    eq(strerror(EIO), "Input/output error", "C strerror(EIO)");
    eq(strerror(EILSEQ), "Invalid or incomplete multibyte or wide character", "C strerror(EILSEQ)");
    eq(strerror(999), "Unknown error 999", "C strerror(999)");
    eq(strerror(-1), "Unknown error -1", "C strerror(-1)");
    eq(strsignal(SIGFPE), "Floating point exception", "C strsignal(SIGFPE)");
    eq(strsignal(SIGCHLD), "Child exited", "C strsignal(SIGCHLD)");
    eq(strsignal(SIGRTMIN + 1), "Real-time signal 1", "C strsignal(SIGRTMIN+1)");
    eq(strsignal(0), "Unknown signal 0", "C strsignal(0)");
    eq(gai_strerror(EAI_NONAME), "Name or service not known", "C gai_strerror(EAI_NONAME)");
    eq(gai_strerror(0), "Success", "C gai_strerror(0)");
    eq(gai_strerror(12345), "Unknown error", "C gai_strerror(12345)");
    char buf[128];
    regerror(REG_EPAREN, nullptr, buf, sizeof(buf));
    eq(buf, "Unmatched ( or \\(", "C regerror(REG_EPAREN)");
    report(regerror(REG_BADPAT, nullptr, buf, 4) == strlen("Invalid regular expression") + 1 &&
           !strcmp(buf, "Inv"), "regerror truncates, returns the full size");
    if (!OSV_CONF_LOCALE) {
        skip("conf_locale=0: no named locales, libc messages stay English");
        return;
    }
    std::string mo = "/usr/share/locale/%s/LC_MESSAGES/libc.mo";
    bool have = true;
    for (const char *l : { "de", "fr", "ja" }) {
        char p[128];
        snprintf(p, sizeof(p), mo.c_str(), l);
        have = have && exists(p);
    }
    if (!have) {
        // conf_locale=1 ships de/fr/ja libc.mo (make-test-locales.sh libc-mo/,
        // copied to /usr/share/locale by the image); absent means the libc
        // message translation path is untested. FAIL at conf1, SKIP at conf0.
        if (OSV_CONF_LOCALE) {
            report(false, "no de/fr/ja libc.mo under /usr/share/locale at conf_locale=1");
        } else {
            skip("no de/fr/ja libc.mo under /usr/share/locale (conf_locale=0)");
        }
        return;
    }
    setlocale(LC_ALL, "de_DE.UTF-8");
    eq(strerror(ENOENT), "Datei oder Verzeichnis nicht gefunden", "de strerror(ENOENT)");
    eq(strerror(999), "Unbekannter Fehler 999", "de strerror(999)");
    eq(strsignal(SIGINT), "Unterbrechung", "de strsignal(SIGINT)");
    eq(strsignal(SIGRTMIN + 1), "Real-Time Signal 1", "de strsignal(SIGRTMIN+1)");
    eq(gai_strerror(EAI_NONAME), "Der Name oder der Dienst ist nicht bekannt", "de gai_strerror(EAI_NONAME)");
    regerror(REG_NOMATCH, nullptr, buf, sizeof(buf));
    eq(buf, "Keine \303\234bereinstimmung gefunden", "de regerror(REG_NOMATCH)");
    snprintf(buf, sizeof(buf), "%s", (errno = EACCES, strerror(errno)));
    eq(buf, "Keine Berechtigung", "de strerror(EACCES)");
    errno = ENOENT;
    snprintf(buf, sizeof(buf), "%m");
    eq(buf, "Datei oder Verzeichnis nicht gefunden", "de printf %m");
    setenv("LANGUAGE", "fr", 1);
    eq(strerror(EPERM), "Op\303\251ration non permise", "LANGUAGE=fr strerror(EPERM)");
    unsetenv("LANGUAGE");
    locale_t ja = newlocale(LC_ALL_MASK, "ja_JP.UTF-8", nullptr);
    eq(strerror_l(ENOENT, ja), "\343\201\235\343\201\256\343\202\210\343\201\206\343\201\252\343"
       "\203\225\343\202\241\343\202\244\343\203\253\343\202\204\343\203\207\343\202\243\343\203"
       "\254\343\202\257\343\203\210\343\203\252\343\201\257\343\201\202\343\202\212\343\201\276"
       "\343\201\233\343\202\223", "strerror_l(ENOENT, ja)");
    eq(strerror(ENOENT), "Datei oder Verzeichnis nicht gefunden", "strerror after strerror_l: still de");
    freelocale(ja);
    setlocale(LC_CTYPE, "C");
    eq(strerror(EILSEQ), "Ung?ltiges oder unvollst?ndiges Multi-Byte- oder Wide-Zeichen",
       "de messages, C LC_CTYPE: transliterated");
    setlocale(LC_ALL, "C");
    eq(strerror(ENOENT), "No such file or directory", "back to C");
    // The spec's case: LANG=de_DE.UTF-8 and setlocale(LC_ALL, "").
    setenv("LANG", "de_DE.UTF-8", 1);
    report(setlocale(LC_ALL, "") != nullptr, "setlocale(LC_ALL, \"\") with LANG=de_DE.UTF-8");
    eq(strerror(ENOENT), "Datei oder Verzeichnis nicht gefunden", "LANG=de_DE.UTF-8 strerror(ENOENT)");
    unsetenv("LANG");
    reset();
}

// --dump <locale>: libc's messages in one locale, one per line, as
// scripts/locale-oracle/messages/lmp.c prints them on glibc ("all"), for
// the on-OSv differential.
static void put(const char *k, long n, const char *s)
{
    printf("%s %ld ", k, n);
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p < 32 || *p == '\\') {
            printf("\\x%02x", *p);
        } else {
            putchar(*p);
        }
    }
    putchar('\n');
}

static int dump(const char *loc)
{
    setlocale(LC_ALL, loc);
    for (int e = -2; e <= 140; e++) {
        put("strerror", e, strerror(e));
    }
    for (int s = -1; s <= 66; s++) {
        // glibc numbers real-time signals from its SIGRTMIN 34, OSv from 35
        // (30 of them, glibc 31): line s holds "Real-time signal s - 34" in
        // each system's numbering; OSv has no line 64's.
        int sig = s >= 34 && s <= 64 ? s - 34 + SIGRTMIN : s;
        put("strsignal", s, strsignal(sig));
    }
    for (int g = -110; g <= 1; g++) {
        put("gai_strerror", g, gai_strerror(g));
    }
    for (int r = 0; r <= 16; r++) {
        char eb[256];
        size_t n = regerror(r, nullptr, eb, sizeof(eb));
        put("regerror", r, eb);
        put("regerror-len", r, n == strlen(eb) + 1 ? "ok" : "BAD");
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[1], "--dump")) {
        return dump(argv[2]);
    }
    for (const char *v : { "LC_ALL", "LANG", "LANGUAGE", "LC_MESSAGES", "LC_CTYPE" }) {
        unsetenv(v);
    }
    const char *d = getenv("OSV_LOCALE_TEST_DATA");
    data_dir = d ? d : "/usr/share/osv-locale-test";
    cats = data_dir + "/msg";
    test_libc();
    if (!exists(data_dir + "/dir/de_DE.utf8/LC_CTYPE") || !exists(cats + "/de/LC_MESSAGES/t1.mo")) {
        // conf_locale=1 images ship the test locales and catalogs
        // (make-test-locales.sh hard-fails the build without localedef/msgfmt),
        // so absent data at conf1 means the gettext engine, codeset conversion
        // and plural selection went untested: a real defect, not a SKIP. A gate
        // that cannot fail is not a gate. conf0 has no catalogs by design.
        if (OSV_CONF_LOCALE) {
            report(false, "no test locales or catalogs in " + data_dir +
                   " at conf_locale=1 (build should have failed without localedef/msgfmt)");
        } else {
            skip("no test locales or catalogs in " + data_dir + " (conf_locale=0)");
        }
    } else if (!OSV_CONF_LOCALE) {
        reset();
        bindtextdomain("t1", cats.c_str());
        setenv("LANGUAGE", "de", 1);
        eq(dgettext("t1", "hello"), "hello", "conf_locale=0: gettext returns the msgid");
        eq(dngettext("t1", "file", "files", 2), "files", "conf_locale=0: ngettext n=2");
        reset();
        skip("conf_locale=0: catalogs");
    } else {
        test_catalogs();
    }
    printf("SUMMARY: %d tests, %d failures, %d skipped\n", tests, fails, skips);
    return fails ? 1 : 0;
}
