/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
/* Host differential: glibc's strptime against OSv's libc/time/strptime.c
 * built for Linux (build.sh) as osv_strptime, with the same compiled locale
 * data (LOCPATH):
 *   spd <locale>...   prints the cases that differ and a SUMMARY line; exit 1
 *   if any differ.
 * Cases: the round trip of glibc's strftime output for every conversion,
 * with and without flags and E/O, on 12 dates; and a fixed list of inputs
 * (edge values, ranges, spacing, case). Each is parsed into a zeroed tm and
 * into one with every field preset, and compared by the end position and
 * every tm field. */
#define _GNU_SOURCE
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
char *osv_strptime(const char *, const char *, struct tm *);
static long ndiff, ntot;
static const char *cur;
static void fmt_tm(char *b, size_t n, const char *in, const char *e, const struct tm *t)
{
    snprintf(b, n, "%s y%d m%d d%d H%d M%d S%d wd%d yd%d dst%d off%ld", e ? (*e ? e : "END") : "NULL",
             t->tm_year, t->tm_mon, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec, t->tm_wday, t->tm_yday,
             t->tm_isdst, t->tm_gmtoff);
    (void)in;
}
static void one(const char *in, const char *f, int preset)
{
    struct tm a, b;
    memset(&a, 0, sizeof a);
    if (preset) {
        a.tm_sec = 7; a.tm_min = 8; a.tm_hour = 9; a.tm_mday = 10; a.tm_mon = 3; a.tm_year = 77;
        a.tm_wday = 5; a.tm_yday = 66; a.tm_isdst = 1; a.tm_gmtoff = 123;
    }
    b = a;
    const char *ea = strptime(in, f, &a), *eb = osv_strptime(in, f, &b);
    char sa[256], sb[256];
    fmt_tm(sa, sizeof sa, in, ea, &a);
    fmt_tm(sb, sizeof sb, in, eb, &b);
    ntot++;
    if (strcmp(sa, sb) || (ea && eb && ea - in != eb - in)) {
        ndiff++;
        if (ndiff < 600) printf("DIFF %s p%d [%s] [%s]\n  glibc %s\n  osv   %s\n", cur, preset, f, in, sa, sb);
    }
}
static void both(const char *in, const char *f)
{
    one(in, f, 0);
    one(in, f, 1);
}
static const char *const fixed[][2] = {
    { "60", "%U" }, { "54", "%U" }, { "53", "%U" }, { "99", "%W" }, { "7", "%w" }, { "60", "%M" }, { "61", "%S" },
    { "62", "%S" }, { "24", "%H" }, { "13", "%I" }, { "0", "%I" }, { "32", "%d" }, { "0", "%d" }, { "123", "%d" },
    { "1234", "%G" }, { "12", "%g" }, { "60", "%V" }, { "2026 3 1", "%Y %U %w" }, { "2026 3 1", "%Y %W %u" },
    { "3 2026", "%u %Y" }, { "5 2026", "%u %Y" }, { "2026 7 Mon", "%Y %m %a" }, { "15 2026", "%d %Y" },
    { "2 15", "%m %d" }, { "100", "%C" }, { "99", "%C" }, { "-5", "%C" }, { "5", "%y" }, { "68", "%y" },
    { "69", "%y" }, { "1", "%Y" }, { "0", "%Y" }, { "9999", "%Y" }, { "12345", "%Y" }, { "-12", "%Y" },
    { "+05:30", "%z" }, { "+0530x", "%z" }, { "-12", "%z" }, { "+1300", "%z" }, { "+2400", "%z" },
    { "+05:3", "%z" }, { "+5", "%z" }, { "+053", "%z" }, { "z", "%z" }, { "Z", "%z" }, { "  +0100", "%z" },
    { "-0330", "%z" }, { "1767225600", "%s" }, { "-5", "%s" }, { "  12", "%s" }, { "x", "%s" },
    { "Thu", "%a" }, { "thursday", "%A" }, { "Thurs", "%a" }, { "Ja", "%b" }, { "January", "%b" },
    { "Janu", "%B" }, { "AMx", "%p" }, { "pm", "%p" }, { "%", " %%" }, { "5%", "%d%%" }, { "x  y", "x%ny" },
    { "xy", "x%ny" }, { "xy", "x y" }, { "x  y", "x%ty" }, { "  5", "%e" }, { "5", "%5d" }, { "5", "%-5d" },
    { "1 2", "%e%e" }, { "12", "%e%e" }, { "5", "%Oe" }, { "5", "%Ee" }, { "5", "%EH" }, { "26", "%Oy" },
    { "26", "%OY" }, { "20", "%OC" }, { "2026", "%EY" }, { "20 26", "%EC %Ey" }, { "20 26", "%EC %y" },
    { "26", "%Ey" }, { "2026 001", "%Y %j" }, { "100", "%j" }, { "367", "%j" }, { "x", "%Q" }, { "x", "%Ex" },
    { "GMT x", "%Z x" }, { "UTC", "%Z" }, { "abc def", "%Z" }, { "GMT,x", "%Z,x" }, { "", "%Z" },
    { "12 PM", "%I %p" }, { "PM 12", "%p %I" }, { "12 AM", "%I %p" }, { "AM 12", "%p %I" }, { "3 PM", "%I %p" },
    { "15 PM", "%H %p" }, { "PM 15", "%p %H" }, { "PM 3", "%p %l" }, { "12 3 PM", "%I %H %p" },
    { "3 PM 5", "%I %p %H" }, { " 3", "%l" }, { "15", "%k" }, { "pm", "%P" }, { "Mon5", "%a%d" },
    { "monday", "%a" }, { "Mo", "%a" }, { "2026 27", "%Y %C" }, { "27 2026", "%C %Y" }, { "26 27", "%y %C" },
    { "2026 60", "%Y %U" }, { "14", "%H%M" }, { "1430", "%H%M" }, { "123456", "%5Y" }, { "1", "%1d" },
    { "2026-02-30", "%F" }, { "2026-13-01", "%F" }, { "2024 366", "%Y %j" }, { "2025 366", "%Y %j" },
    { "2026 0 0", "%Y %U %w" }, { "2026 53 6", "%Y %U %w" }, { "2026 1 7", "%G %V %u" }, { "Thu 2026", "%a %Y" },
    { "2026 Feb", "%Y %b" }, { "Feb 2026", "%b %Y" }, { "2026 5", "%Y %e" }, { "x", "x" }, { "X", "x" },
    { "", "" }, { "", "%n" }, { "abc", "" }, { "5 ", "%d " }, { "5", "%d " }, { "  ", "%t" },
    { "1 2 3", "%D" }, { "01/02/03", "%D" }, { "1:2", "%R" }, { "1:2:3", "%T" }, { "13:45:30", "%r" },
    { "1:45:30 PM", "%r" }, { "%", "%" }, { "%x", "%%%x" }, { "5", "%_d" }, { "5", "%^d" }, { "5", "%#d" },
    { "5", "%0d" }, { "MON", "%^a" }, { "2026", "%+Y" }, { "2026", "%-4Y" },
    { "01 01 26", "%Od %Om %Oy" }, { "01 01", "%Od %Om" }, { "01 01", "%d %Om" }, { "01 01", "%Od %d" },
    { "00:00:00", "%OH:%OM:%OS" }, { "00:00", "%OH:%M" }, { "00 00", "%OM %OM" }, { "一 一", "%Od %Om" },
    { "一 01", "%Od %Om" }, { "01 一", "%Od %Om" }, { "十五:三", "%OH:%OM" }, { "۰۵ ۰۱", "%Od %Om" },
    { "05 ۰۱", "%Od %Om" }, { "۰۵ 01", "%Od %Om" },
};
int main(int argc, char **argv)
{
    static const char convs[] = "aAbBcCdDeFgGhHIjklmMnpPrRsStTuUVwWxXyYzZ%";
    static const char *flags[] = { "", "-", "_", "0", "^", "#" };
    static const char *mods[] = { "", "E", "O" };
    setenv("TZ", "UTC0", 1);
    tzset();
    for (int li = 1; li < argc; li++) {
        if (!setlocale(LC_ALL, argv[li])) { printf("NOLOCALE %s\n", argv[li]); ndiff++; continue; }
        cur = argv[li];
        static const int ymd[][6] = {
            { 2026, 1, 1, 0, 0, 0 }, { 2026, 7, 15, 13, 45, 30 }, { 1999, 12, 31, 23, 59, 59 },
            { 2019, 5, 3, 0, 7, 9 }, { 1989, 1, 7, 12, 0, 0 }, { 1989, 1, 8, 12, 30, 0 },
            { 2020, 12, 31, 12, 0, 0 }, { 1912, 7, 30, 1, 2, 3 }, { 2000, 2, 29, 11, 59, 59 },
            { 1970, 1, 1, 0, 0, 0 }, { 2038, 1, 19, 3, 14, 7 }, { 1926, 12, 25, 18, 0, 0 },
        };
        for (unsigned di = 0; di < sizeof ymd / sizeof *ymd; di++) {
            struct tm tm;
            memset(&tm, 0, sizeof tm);
            tm.tm_year = ymd[di][0] - 1900; tm.tm_mon = ymd[di][1] - 1; tm.tm_mday = ymd[di][2];
            tm.tm_hour = ymd[di][3]; tm.tm_min = ymd[di][4]; tm.tm_sec = ymd[di][5];
            time_t t = timegm(&tm);
            gmtime_r(&t, &tm);
            for (const char *c = convs; *c; c++)
                for (unsigned mi = 0; mi < 3; mi++)
                    for (unsigned fi = 0; fi < 6; fi++) {
                        char f[16], g[16], buf[512];
                        snprintf(f, sizeof f, "%%%s%s%c", flags[fi], mods[mi], *c);
                        if (!strftime(buf, sizeof buf, f, &tm)) continue;
                        both(buf, f);
                        snprintf(g, sizeof g, "%%%s%c", mods[mi], *c);
                        if (fi) both(buf, g);
                    }
            static const char *const comp[] = { "%c|%x|%X", "%Ec", "%Ex %EX", "%x %r", "%a %b %e %H:%M:%S %Y",
                                                "%A, %d %B %Y %Z", "%G-W%V-%u", "%Y %U %w", "%Y %W %u", "%Y %j %T",
                                                "%C%y %m %d", "%EC %Ey", "%EY %m %d", "%p %I:%M", "%I:%M %p",
                                                "%Od %Om %Oy", "%OH:%OM:%OS", "%Oe %OB %OI %p", "%OU %Ow %Y" };
            for (unsigned k = 0; k < sizeof comp / sizeof *comp; k++) {
                char buf[512];
                if (strftime(buf, sizeof buf, comp[k], &tm)) both(buf, comp[k]);
            }
        }
        for (unsigned k = 0; k < sizeof fixed / sizeof *fixed; k++) both(fixed[k][0], fixed[k][1]);
    }
    printf("SUMMARY %ld of %ld differ\n", ndiff, ntot);
    return ndiff != 0;
}
