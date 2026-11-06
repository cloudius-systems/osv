/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Dump what a named locale drives, for the differential against glibc with
// the same compiled data (scripts/locale-oracle/README.md):
//   misc-locale-dump <name>      (LOCPATH selects a directory; unset: the archive)
// Lines starting "@ " are the dump; anything else is commentary.
//   @ setlocale <name or NULL errno>     and setlocale(LC_ALL, NULL) after it
//   @ item <item> <cat> <index> <type> <value>   every nl-typed.txt item:
//       s string, hex bytes; b byte, hex of the first byte (the grouping
//       strings to their NUL); d word, decimal of its 32 bits; w wide
//       string, hex code points; r the two conversion-rate words; x an
//       internal table, not printed
//   @ name <cat> <_NL_LOCALE_NAME(cat)>
//   @ newlocale <agrees|mismatch item>  nl_langinfo_l(newlocale(name)) vs nl_langinfo
//   @ lconv <field> <value>             localeconv(): strings hex, chars decimal
//   @ strftime <date> %<c> <hex>         each POSIX conversion, 3 fixed dates (TZ=UTC0)
//   @ strptime <date> %<c> <consumed|NULL> y m d H M S   round trip of %c %x %X
//   @ strftime2 <date> <formats> <hex>   GNU flags, widths, %E eras, %O digits
//   @ strptime2 <date> <format> <end|partial|NULL> y m d H M S wday yday
//   @ strfmon <format> <amount> <return> <hex|errno>
//   @ rpmatch <hex answer> <result>

#include <cerrno>
#include <clocale>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <langinfo.h>
#include <locale.h>
#include <monetary.h>

namespace {

// Every langinfo item: name, category, index, type (nl-typed.txt, with
// glibc's internal LC_CTYPE/LC_COLLATE tables as x).
struct item {
    const char *name;
    int cat, idx;
    char type;
} const items[] = {
    { "ABDAY_1", 2, 0, 's' },
    { "ABDAY_2", 2, 1, 's' },
    { "ABDAY_3", 2, 2, 's' },
    { "ABDAY_4", 2, 3, 's' },
    { "ABDAY_5", 2, 4, 's' },
    { "ABDAY_6", 2, 5, 's' },
    { "ABDAY_7", 2, 6, 's' },
    { "DAY_1", 2, 7, 's' },
    { "DAY_2", 2, 8, 's' },
    { "DAY_3", 2, 9, 's' },
    { "DAY_4", 2, 10, 's' },
    { "DAY_5", 2, 11, 's' },
    { "DAY_6", 2, 12, 's' },
    { "DAY_7", 2, 13, 's' },
    { "ABMON_1", 2, 14, 's' },
    { "ABMON_2", 2, 15, 's' },
    { "ABMON_3", 2, 16, 's' },
    { "ABMON_4", 2, 17, 's' },
    { "ABMON_5", 2, 18, 's' },
    { "ABMON_6", 2, 19, 's' },
    { "ABMON_7", 2, 20, 's' },
    { "ABMON_8", 2, 21, 's' },
    { "ABMON_9", 2, 22, 's' },
    { "ABMON_10", 2, 23, 's' },
    { "ABMON_11", 2, 24, 's' },
    { "ABMON_12", 2, 25, 's' },
    { "MON_1", 2, 26, 's' },
    { "MON_2", 2, 27, 's' },
    { "MON_3", 2, 28, 's' },
    { "MON_4", 2, 29, 's' },
    { "MON_5", 2, 30, 's' },
    { "MON_6", 2, 31, 's' },
    { "MON_7", 2, 32, 's' },
    { "MON_8", 2, 33, 's' },
    { "MON_9", 2, 34, 's' },
    { "MON_10", 2, 35, 's' },
    { "MON_11", 2, 36, 's' },
    { "MON_12", 2, 37, 's' },
    { "AM_STR", 2, 38, 's' },
    { "PM_STR", 2, 39, 's' },
    { "D_T_FMT", 2, 40, 's' },
    { "D_FMT", 2, 41, 's' },
    { "T_FMT", 2, 42, 's' },
    { "T_FMT_AMPM", 2, 43, 's' },
    { "ERA", 2, 44, 's' },
    { "__ERA_YEAR", 2, 45, 's' },
    { "ERA_D_FMT", 2, 46, 's' },
    { "ALT_DIGITS", 2, 47, 's' },
    { "ERA_D_T_FMT", 2, 48, 's' },
    { "ERA_T_FMT", 2, 49, 's' },
    { "_NL_TIME_ERA_NUM_ENTRIES", 2, 50, 'd' },
    { "_NL_TIME_ERA_ENTRIES", 2, 51, 's' },
    { "_NL_WABDAY_1", 2, 52, 'w' },
    { "_NL_WABDAY_2", 2, 53, 'w' },
    { "_NL_WABDAY_3", 2, 54, 'w' },
    { "_NL_WABDAY_4", 2, 55, 'w' },
    { "_NL_WABDAY_5", 2, 56, 'w' },
    { "_NL_WABDAY_6", 2, 57, 'w' },
    { "_NL_WABDAY_7", 2, 58, 'w' },
    { "_NL_WDAY_1", 2, 59, 'w' },
    { "_NL_WDAY_2", 2, 60, 'w' },
    { "_NL_WDAY_3", 2, 61, 'w' },
    { "_NL_WDAY_4", 2, 62, 'w' },
    { "_NL_WDAY_5", 2, 63, 'w' },
    { "_NL_WDAY_6", 2, 64, 'w' },
    { "_NL_WDAY_7", 2, 65, 'w' },
    { "_NL_WABMON_1", 2, 66, 'w' },
    { "_NL_WABMON_2", 2, 67, 'w' },
    { "_NL_WABMON_3", 2, 68, 'w' },
    { "_NL_WABMON_4", 2, 69, 'w' },
    { "_NL_WABMON_5", 2, 70, 'w' },
    { "_NL_WABMON_6", 2, 71, 'w' },
    { "_NL_WABMON_7", 2, 72, 'w' },
    { "_NL_WABMON_8", 2, 73, 'w' },
    { "_NL_WABMON_9", 2, 74, 'w' },
    { "_NL_WABMON_10", 2, 75, 'w' },
    { "_NL_WABMON_11", 2, 76, 'w' },
    { "_NL_WABMON_12", 2, 77, 'w' },
    { "_NL_WMON_1", 2, 78, 'w' },
    { "_NL_WMON_2", 2, 79, 'w' },
    { "_NL_WMON_3", 2, 80, 'w' },
    { "_NL_WMON_4", 2, 81, 'w' },
    { "_NL_WMON_5", 2, 82, 'w' },
    { "_NL_WMON_6", 2, 83, 'w' },
    { "_NL_WMON_7", 2, 84, 'w' },
    { "_NL_WMON_8", 2, 85, 'w' },
    { "_NL_WMON_9", 2, 86, 'w' },
    { "_NL_WMON_10", 2, 87, 'w' },
    { "_NL_WMON_11", 2, 88, 'w' },
    { "_NL_WMON_12", 2, 89, 'w' },
    { "_NL_WAM_STR", 2, 90, 'w' },
    { "_NL_WPM_STR", 2, 91, 'w' },
    { "_NL_WD_T_FMT", 2, 92, 'w' },
    { "_NL_WD_FMT", 2, 93, 'w' },
    { "_NL_WT_FMT", 2, 94, 'w' },
    { "_NL_WT_FMT_AMPM", 2, 95, 'w' },
    { "_NL_WERA_YEAR", 2, 96, 'w' },
    { "_NL_WERA_D_FMT", 2, 97, 'w' },
    { "_NL_WALT_DIGITS", 2, 98, 'w' },
    { "_NL_WERA_D_T_FMT", 2, 99, 'w' },
    { "_NL_WERA_T_FMT", 2, 100, 'w' },
    { "_NL_TIME_WEEK_NDAYS", 2, 101, 'b' },
    { "_NL_TIME_WEEK_1STDAY", 2, 102, 'd' },
    { "_NL_TIME_WEEK_1STWEEK", 2, 103, 'b' },
    { "_NL_TIME_FIRST_WEEKDAY", 2, 104, 'b' },
    { "_NL_TIME_FIRST_WORKDAY", 2, 105, 'b' },
    { "_NL_TIME_CAL_DIRECTION", 2, 106, 'b' },
    { "_NL_TIME_TIMEZONE", 2, 107, 's' },
    { "_NL_W_DATE_FMT", 2, 109, 'w' },
    { "_NL_TIME_CODESET", 2, 110, 's' },
    { "__ALTMON_1", 2, 111, 's' },
    { "__ALTMON_2", 2, 112, 's' },
    { "__ALTMON_3", 2, 113, 's' },
    { "__ALTMON_4", 2, 114, 's' },
    { "__ALTMON_5", 2, 115, 's' },
    { "__ALTMON_6", 2, 116, 's' },
    { "__ALTMON_7", 2, 117, 's' },
    { "__ALTMON_8", 2, 118, 's' },
    { "__ALTMON_9", 2, 119, 's' },
    { "__ALTMON_10", 2, 120, 's' },
    { "__ALTMON_11", 2, 121, 's' },
    { "__ALTMON_12", 2, 122, 's' },
    { "_NL_WALTMON_1", 2, 123, 'w' },
    { "_NL_WALTMON_2", 2, 124, 'w' },
    { "_NL_WALTMON_3", 2, 125, 'w' },
    { "_NL_WALTMON_4", 2, 126, 'w' },
    { "_NL_WALTMON_5", 2, 127, 'w' },
    { "_NL_WALTMON_6", 2, 128, 'w' },
    { "_NL_WALTMON_7", 2, 129, 'w' },
    { "_NL_WALTMON_8", 2, 130, 'w' },
    { "_NL_WALTMON_9", 2, 131, 'w' },
    { "_NL_WALTMON_10", 2, 132, 'w' },
    { "_NL_WALTMON_11", 2, 133, 'w' },
    { "_NL_WALTMON_12", 2, 134, 'w' },
    { "_NL_ABALTMON_1", 2, 135, 's' },
    { "_NL_ABALTMON_2", 2, 136, 's' },
    { "_NL_ABALTMON_3", 2, 137, 's' },
    { "_NL_ABALTMON_4", 2, 138, 's' },
    { "_NL_ABALTMON_5", 2, 139, 's' },
    { "_NL_ABALTMON_6", 2, 140, 's' },
    { "_NL_ABALTMON_7", 2, 141, 's' },
    { "_NL_ABALTMON_8", 2, 142, 's' },
    { "_NL_ABALTMON_9", 2, 143, 's' },
    { "_NL_ABALTMON_10", 2, 144, 's' },
    { "_NL_ABALTMON_11", 2, 145, 's' },
    { "_NL_ABALTMON_12", 2, 146, 's' },
    { "_NL_WABALTMON_1", 2, 147, 'w' },
    { "_NL_WABALTMON_2", 2, 148, 'w' },
    { "_NL_WABALTMON_3", 2, 149, 'w' },
    { "_NL_WABALTMON_4", 2, 150, 'w' },
    { "_NL_WABALTMON_5", 2, 151, 'w' },
    { "_NL_WABALTMON_6", 2, 152, 'w' },
    { "_NL_WABALTMON_7", 2, 153, 'w' },
    { "_NL_WABALTMON_8", 2, 154, 'w' },
    { "_NL_WABALTMON_9", 2, 155, 'w' },
    { "_NL_WABALTMON_10", 2, 156, 'w' },
    { "_NL_WABALTMON_11", 2, 157, 'w' },
    { "_NL_WABALTMON_12", 2, 158, 'w' },
    { "_NL_COLLATE_NRULES", 3, 0, 'd' },
    { "_NL_COLLATE_RULESETS", 3, 1, 'x' },
    { "_NL_COLLATE_TABLEMB", 3, 2, 'x' },
    { "_NL_COLLATE_WEIGHTMB", 3, 3, 'x' },
    { "_NL_COLLATE_EXTRAMB", 3, 4, 'x' },
    { "_NL_COLLATE_INDIRECTMB", 3, 5, 'x' },
    { "_NL_COLLATE_TABLEWC", 3, 9, 'x' },
    { "_NL_COLLATE_WEIGHTWC", 3, 10, 'x' },
    { "_NL_COLLATE_EXTRAWC", 3, 11, 'x' },
    { "_NL_COLLATE_INDIRECTWC", 3, 12, 'x' },
    { "_NL_COLLATE_SYMB_HASH_SIZEMB", 3, 13, 'd' },
    { "_NL_COLLATE_SYMB_TABLEMB", 3, 14, 'x' },
    { "_NL_COLLATE_SYMB_EXTRAMB", 3, 15, 'x' },
    { "_NL_COLLATE_COLLSEQMB", 3, 16, 'x' },
    { "_NL_COLLATE_COLLSEQWC", 3, 17, 'x' },
    { "_NL_COLLATE_CODESET", 3, 18, 's' },
    { "_NL_CTYPE_CLASS", 0, 0, 'x' },
    { "_NL_CTYPE_TOUPPER", 0, 1, 'x' },
    { "_NL_CTYPE_TOLOWER", 0, 3, 'x' },
    { "_NL_CTYPE_CLASS32", 0, 5, 'x' },
    { "_NL_CTYPE_CLASS_NAMES", 0, 10, 's' },
    { "_NL_CTYPE_MAP_NAMES", 0, 11, 's' },
    { "_NL_CTYPE_WIDTH", 0, 12, 'x' },
    { "_NL_CTYPE_MB_CUR_MAX", 0, 13, 'd' },
    { "_NL_CTYPE_CODESET_NAME", 0, 14, 's' },
    { "_NL_CTYPE_TOUPPER32", 0, 15, 'x' },
    { "_NL_CTYPE_TOLOWER32", 0, 16, 'x' },
    { "_NL_CTYPE_CLASS_OFFSET", 0, 17, 'd' },
    { "_NL_CTYPE_MAP_OFFSET", 0, 18, 'd' },
    { "_NL_CTYPE_INDIGITS_MB_LEN", 0, 19, 'd' },
    { "_NL_CTYPE_INDIGITS0_MB", 0, 20, 's' },
    { "_NL_CTYPE_INDIGITS1_MB", 0, 21, 's' },
    { "_NL_CTYPE_INDIGITS2_MB", 0, 22, 's' },
    { "_NL_CTYPE_INDIGITS3_MB", 0, 23, 's' },
    { "_NL_CTYPE_INDIGITS4_MB", 0, 24, 's' },
    { "_NL_CTYPE_INDIGITS5_MB", 0, 25, 's' },
    { "_NL_CTYPE_INDIGITS6_MB", 0, 26, 's' },
    { "_NL_CTYPE_INDIGITS7_MB", 0, 27, 's' },
    { "_NL_CTYPE_INDIGITS8_MB", 0, 28, 's' },
    { "_NL_CTYPE_INDIGITS9_MB", 0, 29, 's' },
    { "_NL_CTYPE_INDIGITS_WC_LEN", 0, 30, 'd' },
    { "_NL_CTYPE_INDIGITS0_WC", 0, 31, 'w' },
    { "_NL_CTYPE_INDIGITS1_WC", 0, 32, 'w' },
    { "_NL_CTYPE_INDIGITS2_WC", 0, 33, 'w' },
    { "_NL_CTYPE_INDIGITS3_WC", 0, 34, 'w' },
    { "_NL_CTYPE_INDIGITS4_WC", 0, 35, 'w' },
    { "_NL_CTYPE_INDIGITS5_WC", 0, 36, 'w' },
    { "_NL_CTYPE_INDIGITS6_WC", 0, 37, 'w' },
    { "_NL_CTYPE_INDIGITS7_WC", 0, 38, 'w' },
    { "_NL_CTYPE_INDIGITS8_WC", 0, 39, 'w' },
    { "_NL_CTYPE_INDIGITS9_WC", 0, 40, 'w' },
    { "_NL_CTYPE_OUTDIGIT0_MB", 0, 41, 's' },
    { "_NL_CTYPE_OUTDIGIT1_MB", 0, 42, 's' },
    { "_NL_CTYPE_OUTDIGIT2_MB", 0, 43, 's' },
    { "_NL_CTYPE_OUTDIGIT3_MB", 0, 44, 's' },
    { "_NL_CTYPE_OUTDIGIT4_MB", 0, 45, 's' },
    { "_NL_CTYPE_OUTDIGIT5_MB", 0, 46, 's' },
    { "_NL_CTYPE_OUTDIGIT6_MB", 0, 47, 's' },
    { "_NL_CTYPE_OUTDIGIT7_MB", 0, 48, 's' },
    { "_NL_CTYPE_OUTDIGIT8_MB", 0, 49, 's' },
    { "_NL_CTYPE_OUTDIGIT9_MB", 0, 50, 's' },
    { "_NL_CTYPE_OUTDIGIT0_WC", 0, 51, 'd' },
    { "_NL_CTYPE_OUTDIGIT1_WC", 0, 52, 'd' },
    { "_NL_CTYPE_OUTDIGIT2_WC", 0, 53, 'd' },
    { "_NL_CTYPE_OUTDIGIT3_WC", 0, 54, 'd' },
    { "_NL_CTYPE_OUTDIGIT4_WC", 0, 55, 'd' },
    { "_NL_CTYPE_OUTDIGIT5_WC", 0, 56, 'd' },
    { "_NL_CTYPE_OUTDIGIT6_WC", 0, 57, 'd' },
    { "_NL_CTYPE_OUTDIGIT7_WC", 0, 58, 'd' },
    { "_NL_CTYPE_OUTDIGIT8_WC", 0, 59, 'd' },
    { "_NL_CTYPE_OUTDIGIT9_WC", 0, 60, 'd' },
    { "_NL_CTYPE_TRANSLIT_TAB_SIZE", 0, 61, 'd' },
    { "_NL_CTYPE_TRANSLIT_FROM_IDX", 0, 62, 'x' },
    { "_NL_CTYPE_TRANSLIT_FROM_TBL", 0, 63, 'x' },
    { "_NL_CTYPE_TRANSLIT_TO_IDX", 0, 64, 'x' },
    { "_NL_CTYPE_TRANSLIT_TO_TBL", 0, 65, 'x' },
    { "_NL_CTYPE_TRANSLIT_DEFAULT_MISSING_LEN", 0, 66, 'd' },
    { "_NL_CTYPE_TRANSLIT_DEFAULT_MISSING", 0, 67, 'x' },
    { "_NL_CTYPE_TRANSLIT_IGNORE_LEN", 0, 68, 'd' },
    { "_NL_CTYPE_TRANSLIT_IGNORE", 0, 69, 'x' },
    { "_NL_CTYPE_MAP_TO_NONASCII", 0, 70, 'd' },
    { "_NL_CTYPE_NONASCII_CASE", 0, 71, 'd' },
    { "__INT_CURR_SYMBOL", 4, 0, 's' },
    { "__CURRENCY_SYMBOL", 4, 1, 's' },
    { "__MON_DECIMAL_POINT", 4, 2, 's' },
    { "__MON_THOUSANDS_SEP", 4, 3, 's' },
    { "__MON_GROUPING", 4, 4, 'b' },
    { "__POSITIVE_SIGN", 4, 5, 's' },
    { "__NEGATIVE_SIGN", 4, 6, 's' },
    { "__INT_FRAC_DIGITS", 4, 7, 'b' },
    { "__FRAC_DIGITS", 4, 8, 'b' },
    { "__P_CS_PRECEDES", 4, 9, 'b' },
    { "__P_SEP_BY_SPACE", 4, 10, 'b' },
    { "__N_CS_PRECEDES", 4, 11, 'b' },
    { "__N_SEP_BY_SPACE", 4, 12, 'b' },
    { "__P_SIGN_POSN", 4, 13, 'b' },
    { "__N_SIGN_POSN", 4, 14, 'b' },
    { "_NL_MONETARY_CRNCYSTR", 4, 15, 's' },
    { "__INT_P_CS_PRECEDES", 4, 16, 'b' },
    { "__INT_P_SEP_BY_SPACE", 4, 17, 'b' },
    { "__INT_N_CS_PRECEDES", 4, 18, 'b' },
    { "__INT_N_SEP_BY_SPACE", 4, 19, 'b' },
    { "__INT_P_SIGN_POSN", 4, 20, 'b' },
    { "__INT_N_SIGN_POSN", 4, 21, 'b' },
    { "_NL_MONETARY_DUO_INT_CURR_SYMBOL", 4, 22, 's' },
    { "_NL_MONETARY_DUO_CURRENCY_SYMBOL", 4, 23, 's' },
    { "_NL_MONETARY_DUO_INT_FRAC_DIGITS", 4, 24, 'b' },
    { "_NL_MONETARY_DUO_FRAC_DIGITS", 4, 25, 'b' },
    { "_NL_MONETARY_DUO_P_CS_PRECEDES", 4, 26, 'b' },
    { "_NL_MONETARY_DUO_P_SEP_BY_SPACE", 4, 27, 'b' },
    { "_NL_MONETARY_DUO_N_CS_PRECEDES", 4, 28, 'b' },
    { "_NL_MONETARY_DUO_N_SEP_BY_SPACE", 4, 29, 'b' },
    { "_NL_MONETARY_DUO_INT_P_CS_PRECEDES", 4, 30, 'b' },
    { "_NL_MONETARY_DUO_INT_P_SEP_BY_SPACE", 4, 31, 'b' },
    { "_NL_MONETARY_DUO_INT_N_CS_PRECEDES", 4, 32, 'b' },
    { "_NL_MONETARY_DUO_INT_N_SEP_BY_SPACE", 4, 33, 'b' },
    { "_NL_MONETARY_DUO_P_SIGN_POSN", 4, 34, 'b' },
    { "_NL_MONETARY_DUO_N_SIGN_POSN", 4, 35, 'b' },
    { "_NL_MONETARY_DUO_INT_P_SIGN_POSN", 4, 36, 'b' },
    { "_NL_MONETARY_DUO_INT_N_SIGN_POSN", 4, 37, 'b' },
    { "_NL_MONETARY_UNO_VALID_FROM", 4, 38, 'd' },
    { "_NL_MONETARY_UNO_VALID_TO", 4, 39, 'd' },
    { "_NL_MONETARY_DUO_VALID_FROM", 4, 40, 'd' },
    { "_NL_MONETARY_DUO_VALID_TO", 4, 41, 'd' },
    { "_NL_MONETARY_CONVERSION_RATE", 4, 42, 'r' },
    { "_NL_MONETARY_DECIMAL_POINT_WC", 4, 43, 'd' },
    { "_NL_MONETARY_THOUSANDS_SEP_WC", 4, 44, 'd' },
    { "_NL_MONETARY_CODESET", 4, 45, 's' },
    { "__DECIMAL_POINT", 1, 0, 's' },
    { "__THOUSANDS_SEP", 1, 1, 's' },
    { "__GROUPING", 1, 2, 'b' },
    { "_NL_NUMERIC_DECIMAL_POINT_WC", 1, 3, 'd' },
    { "_NL_NUMERIC_THOUSANDS_SEP_WC", 1, 4, 'd' },
    { "_NL_NUMERIC_CODESET", 1, 5, 's' },
    { "__YESEXPR", 5, 0, 's' },
    { "__NOEXPR", 5, 1, 's' },
    { "__YESSTR", 5, 2, 's' },
    { "__NOSTR", 5, 3, 's' },
    { "_NL_MESSAGES_CODESET", 5, 4, 's' },
    { "_NL_PAPER_HEIGHT", 7, 0, 'd' },
    { "_NL_PAPER_WIDTH", 7, 1, 'd' },
    { "_NL_PAPER_CODESET", 7, 2, 's' },
    { "_NL_NAME_NAME_FMT", 8, 0, 's' },
    { "_NL_NAME_NAME_GEN", 8, 1, 's' },
    { "_NL_NAME_NAME_MR", 8, 2, 's' },
    { "_NL_NAME_NAME_MRS", 8, 3, 's' },
    { "_NL_NAME_NAME_MISS", 8, 4, 's' },
    { "_NL_NAME_NAME_MS", 8, 5, 's' },
    { "_NL_NAME_CODESET", 8, 6, 's' },
    { "_NL_ADDRESS_POSTAL_FMT", 9, 0, 's' },
    { "_NL_ADDRESS_COUNTRY_NAME", 9, 1, 's' },
    { "_NL_ADDRESS_COUNTRY_POST", 9, 2, 's' },
    { "_NL_ADDRESS_COUNTRY_AB2", 9, 3, 's' },
    { "_NL_ADDRESS_COUNTRY_AB3", 9, 4, 's' },
    { "_NL_ADDRESS_COUNTRY_CAR", 9, 5, 's' },
    { "_NL_ADDRESS_COUNTRY_NUM", 9, 6, 'd' },
    { "_NL_ADDRESS_COUNTRY_ISBN", 9, 7, 's' },
    { "_NL_ADDRESS_LANG_NAME", 9, 8, 's' },
    { "_NL_ADDRESS_LANG_AB", 9, 9, 's' },
    { "_NL_ADDRESS_LANG_TERM", 9, 10, 's' },
    { "_NL_ADDRESS_LANG_LIB", 9, 11, 's' },
    { "_NL_ADDRESS_CODESET", 9, 12, 's' },
    { "_NL_TELEPHONE_TEL_INT_FMT", 10, 0, 's' },
    { "_NL_TELEPHONE_TEL_DOM_FMT", 10, 1, 's' },
    { "_NL_TELEPHONE_INT_SELECT", 10, 2, 's' },
    { "_NL_TELEPHONE_INT_PREFIX", 10, 3, 's' },
    { "_NL_TELEPHONE_CODESET", 10, 4, 's' },
    { "_NL_MEASUREMENT_MEASUREMENT", 11, 0, 'b' },
    { "_NL_MEASUREMENT_CODESET", 11, 1, 's' },
    { "_NL_IDENTIFICATION_TITLE", 12, 0, 's' },
    { "_NL_IDENTIFICATION_SOURCE", 12, 1, 's' },
    { "_NL_IDENTIFICATION_ADDRESS", 12, 2, 's' },
    { "_NL_IDENTIFICATION_CONTACT", 12, 3, 's' },
    { "_NL_IDENTIFICATION_EMAIL", 12, 4, 's' },
    { "_NL_IDENTIFICATION_TEL", 12, 5, 's' },
    { "_NL_IDENTIFICATION_FAX", 12, 6, 's' },
    { "_NL_IDENTIFICATION_LANGUAGE", 12, 7, 's' },
    { "_NL_IDENTIFICATION_TERRITORY", 12, 8, 's' },
    { "_NL_IDENTIFICATION_AUDIENCE", 12, 9, 's' },
    { "_NL_IDENTIFICATION_APPLICATION", 12, 10, 's' },
    { "_NL_IDENTIFICATION_ABBREVIATION", 12, 11, 's' },
    { "_NL_IDENTIFICATION_REVISION", 12, 12, 's' },
    { "_NL_IDENTIFICATION_DATE", 12, 13, 's' },
    { "_NL_IDENTIFICATION_CATEGORY", 12, 14, 's' },
    { "_NL_IDENTIFICATION_CODESET", 12, 15, 's' },
    { "_DATE_FMT", 2, 108, 's' },
};

void hex(const char *s)
{
    for (; *s; s++) {
        printf("%02x", static_cast<unsigned char>(*s));
    }
}

void value(const item &it, const char *v)
{
    switch (it.type) {
    case 's':
        hex(v);
        break;
    case 'b':
        if (!strcmp(it.name, "__GROUPING") || !strcmp(it.name, "__MON_GROUPING")) {
            hex(v);
        } else {
            printf("%02x", static_cast<unsigned char>(*v));
        }
        break;
    case 'd':
        printf("%u", unsigned(uint32_t(uintptr_t(v))));
        break;
    case 'w':
        for (const uint32_t *w = reinterpret_cast<const uint32_t *>(v); *w; w++) {
            printf("%x,", unsigned(*w));
        }
        break;
    case 'r': {
        const uint32_t *w = reinterpret_cast<const uint32_t *>(v);
        printf("%u,%u", unsigned(w[0]), unsigned(w[1]));
        break;
    }
    default:
        printf("-");
    }
}

// Do two values of an item agree (by content; the pointers may differ)?
bool same(const item &it, const char *a, const char *b)
{
    switch (it.type) {
    case 's':
        return !strcmp(a, b);
    case 'b':
        return *a == *b;
    case 'd':
        return uint32_t(uintptr_t(a)) == uint32_t(uintptr_t(b));
    case 'w': {
        const uint32_t *x = reinterpret_cast<const uint32_t *>(a);
        const uint32_t *y = reinterpret_cast<const uint32_t *>(b);
        for (; *x && *x == *y; x++, y++) {
        }
        return *x == *y;
    }
    case 'r':
        return !memcmp(a, b, 8);
    default:
        return true;
    }
}

const char *const cat_names[13] = {
    "LC_CTYPE", "LC_NUMERIC", "LC_TIME", "LC_COLLATE", "LC_MONETARY", "LC_MESSAGES",
    "LC_ALL", "LC_PAPER", "LC_NAME", "LC_ADDRESS", "LC_TELEPHONE", "LC_MEASUREMENT",
    "LC_IDENTIFICATION",
};

void lconv_str(const char *f, const char *v)
{
    printf("@ lconv %s ", f);
    hex(v);
    printf("\n");
}

void lconv_chr(const char *f, char v)
{
    printf("@ lconv %s %d\n", f, v);
}

} // namespace

int main(int argc, char **argv)
{
    const char *name = argc > 1 ? argv[1] : "C";
    setenv("TZ", "UTC0", 1);
    tzset();
    errno = 0;
    const char *r = setlocale(LC_ALL, name);
    if (!r) {
        printf("@ setlocale NULL %d\n", errno);
        return 0;
    }
    printf("@ setlocale %s\n", r);
    printf("@ setlocale-all %s\n", setlocale(LC_ALL, nullptr));
    for (int c = 0; c < 13; c++) {
        if (c != LC_ALL) {
            printf("@ name %s %s\n", cat_names[c], nl_langinfo((c << 16) | 0xffff));
        }
    }
    locale_t l = newlocale(LC_ALL_MASK, name, nullptr);
    const char *mismatch = l ? nullptr : "newlocale-failed";
    for (const item &it : items) {
        const char *v = nl_langinfo((it.cat << 16) | it.idx);
        printf("@ item %s %d %d %c ", it.name, it.cat, it.idx, it.type);
        value(it, v);
        printf("\n");
        if (l && !mismatch && !same(it, v, nl_langinfo_l((it.cat << 16) | it.idx, l))) {
            mismatch = it.name;
        }
    }
    printf("@ newlocale %s\n", mismatch ? mismatch : "agrees");
    if (l) {
        freelocale(l);
    }
    struct lconv *lc = localeconv();
    lconv_str("decimal_point", lc->decimal_point);
    lconv_str("thousands_sep", lc->thousands_sep);
    lconv_str("grouping", lc->grouping);
    lconv_str("int_curr_symbol", lc->int_curr_symbol);
    lconv_str("currency_symbol", lc->currency_symbol);
    lconv_str("mon_decimal_point", lc->mon_decimal_point);
    lconv_str("mon_thousands_sep", lc->mon_thousands_sep);
    lconv_str("mon_grouping", lc->mon_grouping);
    lconv_str("positive_sign", lc->positive_sign);
    lconv_str("negative_sign", lc->negative_sign);
    lconv_chr("int_frac_digits", lc->int_frac_digits);
    lconv_chr("frac_digits", lc->frac_digits);
    lconv_chr("p_cs_precedes", lc->p_cs_precedes);
    lconv_chr("p_sep_by_space", lc->p_sep_by_space);
    lconv_chr("n_cs_precedes", lc->n_cs_precedes);
    lconv_chr("n_sep_by_space", lc->n_sep_by_space);
    lconv_chr("p_sign_posn", lc->p_sign_posn);
    lconv_chr("n_sign_posn", lc->n_sign_posn);
    lconv_chr("int_p_cs_precedes", lc->int_p_cs_precedes);
    lconv_chr("int_p_sep_by_space", lc->int_p_sep_by_space);
    lconv_chr("int_n_cs_precedes", lc->int_n_cs_precedes);
    lconv_chr("int_n_sep_by_space", lc->int_n_sep_by_space);
    lconv_chr("int_p_sign_posn", lc->int_p_sign_posn);
    lconv_chr("int_n_sign_posn", lc->int_n_sign_posn);
    // 2026-01-01 00:00:00 (Thursday, AM), 2026-07-15 13:45:30 (Wednesday,
    // PM), 1999-12-31 23:59:59 (Friday).
    const time_t dates[] = { 1767225600, 1784123130, 946684799 };
    const char convs[] = "aAbBcCdDeFgGhHIjmMnprRStTuUVwWxXyYzZ%";
    for (int d = 0; d < 3; d++) {
        struct tm tm;
        localtime_r(&dates[d], &tm);
        for (const char *c = convs; *c; c++) {
            char fmt[3] = { '%', *c, 0 }, buf[512];
            size_t n = strftime(buf, sizeof(buf), fmt, &tm);
            printf("@ strftime %d %%%c ", d, *c);
            if (n) {
                hex(buf);
            } else {
                printf("(0)");
            }
            printf("\n");
        }
        for (const char *c = "cxX"; *c; c++) {
            char fmt[3] = { '%', *c, 0 }, buf[512];
            strftime(buf, sizeof(buf), fmt, &tm);
            struct tm p;
            memset(&p, 0, sizeof(p));
            const char *end = strptime(buf, fmt, &p);
            printf("@ strptime %d %%%c ", d, *c);
            if (end) {
                printf("%d", int(end - buf));
            } else {
                printf("NULL");
            }
            printf(" %d %d %d %d %d %d\n", p.tm_year, p.tm_mon, p.tm_mday, p.tm_hour, p.tm_min,
                   p.tm_sec);
        }
    }
    // GNU flags, widths, eras and alternative digits.
    static const char *const fmts[] = {
        "%-d|%_d|%0e|%^a|%#A|%^B|%#b|%10A|%-10A|%010A|%5d|%_5H|%-5M|%^p|%#p|%P|%#Z|%^Z",
        "%k|%l|%P|%s|%3Y|%_10Y|%-y|%10z|%_z|%-z|%5n|%5%|%Q|%5Q|%^Q",
        "%EC|%Ey|%EY|%Ec|%Ex|%EX|%_EY|%-Ey|%10EC|%^EY",
        "%Od|%Oe|%OH|%OI|%Om|%OM|%OS|%Ou|%OU|%OV|%Ow|%OW|%Oy|%OB|%Ob|%Oh|%Ok|%Ol|%5Od|%_Oy",
        "%r|%^r|%10r|%c|%^c|%_x|%-X",
    };
    // 2019-05-03 00:07:09 (an era's first year), 1989-01-07 12:00:00 (an
    // era's last day), 1912-07-30 01:02:03, 0005-01-05 (years of 1 digit).
    const time_t more[] = { 1767225600, 1784123130, 1556842029, 600177600, -1812149877,
                            -62009068719LL };
    for (int d = 0; d < 6; d++) {
        struct tm tm;
        localtime_r(&more[d], &tm);   // TZ=UTC0: %Z is "UTC" on both (gmtime: glibc "GMT", musl "UTC")
        for (const char *f : fmts) {
            char buf[1024];
            size_t n = strftime(buf, sizeof(buf), f, &tm);
            printf("@ strftime2 %d %s ", d, f);
            if (n) {
                hex(buf);
            } else {
                printf("(0)");
            }
            printf("\n");
        }
        static const char *const rt[] = { "%c", "%x", "%X", "%r", "%Ec", "%Ex", "%EX", "%EY",
                                          "%EC %Ey", "%Od %Om %Oy", "%OH:%OM:%OS", "%a %b %e %Y",
                                          "%A %B %d %Y %p %I", "%j %Y", "%U %w %Y", "%G-%V-%u" };
        for (const char *f : rt) {
            char buf[512];
            if (!strftime(buf, sizeof(buf), f, &tm)) {
                continue;
            }
            struct tm p;
            memset(&p, 0, sizeof(p));
            const char *end = strptime(buf, f, &p);
            printf("@ strptime2 %d %s %s %d %d %d %d %d %d %d %d\n", d, f,
                   end ? (*end ? "partial" : "end") : "NULL", p.tm_year, p.tm_mon, p.tm_mday,
                   p.tm_hour, p.tm_min, p.tm_sec, p.tm_wday, p.tm_yday);
        }
    }
    // strfmon over fixed amounts and formats; rpmatch.
    static const char *const mfmts[] = { "%n", "%i", "%=*#10.2n", "%^n", "%!n", "%(n", "%+n",
                                         "%-14n", "%14i", "%#5n", "%(#5i", "%.0n", "%=0#8.3i" };
    static const double amounts[] = { 0, 1.5, -0.5, 1234567.891, -1234567.891, 0.125 };
    for (const char *f : mfmts) {
        for (double v : amounts) {
            char buf[256];
            errno = 0;
            ssize_t n = strfmon(buf, sizeof(buf), f, v);
            printf("@ strfmon %s %lld %zd ", f, (long long)(v * 1000), n);   // milli-units: no locale decimal point
            if (n >= 0) {
                hex(buf);
            } else {
                printf("errno=%d", errno);
            }
            printf("\n");
        }
    }
    static const char *const answers[] = { "y", "Yes", "n", "no", "j", "ja", "nein", "1", "0", "+",
                                           "-", "", " y", "x", "\u0434\u0430", "\u043d\u0435\u0442",
                                           "\u306f\u3044", "\u3044\u3044\u3048", "evet", "e", "h", "o" };
    for (const char *a : answers) {
        printf("@ rpmatch ");
        hex(a);
        printf(" %d\n", rpmatch(a));
    }
    return 0;
}
