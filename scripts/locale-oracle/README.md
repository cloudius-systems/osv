The values glibc 2.42 (x86_64) returns from `nl_langinfo_l(item, newlocale(LC_ALL_MASK, name, 0))`
for its C and C.UTF-8 locales, for every item of LC_NUMERIC, LC_TIME, LC_MONETARY, LC_MESSAGES,
LC_PAPER, LC_NAME, LC_ADDRESS, LC_TELEPHONE, LC_MEASUREMENT and LC_IDENTIFICATION.

Each row is `<item> <category> <index> <type> <value>`. Types: `s` string (hex bytes), `b` byte
item (hex, read up to a NUL, so only the first byte is the item), `d` word (decimal: glibc returns
the value in the pointer bits), `w` wide string (code points).

`scripts/gen-langinfo-builtin.py` turns these into `libc/locale/langinfo-builtin.inc`.

## Wide-character tables (glibc 2.41, Debian 13)

`libc/locale/ctype-utf8.inc` and `libc/locale/translit-c.inc` are generated from the measured
runtime behaviour of glibc 2.41 (libc6 2.41-12+deb13u4, amd64, Debian 13). No glibc source or locale
data file was read. The inputs, and which program produced each:

| file | produced by | content |
|---|---|---|
| `d-glibc-C.UTF-8.txt` (not committed, 25762437 bytes, sha256 `9d936c006641ce3f407d9f7ad2742bd7e54e138235e580221f57aecdf0a1c0ab`) | `gen-ctype-oracle C.UTF-8` | per code point 0..0x10ffff: the 12 `iswctype()` classes, `towupper()`, `towlower()`, `wcwidth()` |
| `d-glibc-C.txt` (not committed, 25611556 bytes, sha256 `a4a5e4022c07899f88a49263c399379160c3598fd740a8595171ba0e23b2421f`) | `gen-ctype-oracle C` | the same for the C locale (ASCII only) |
| `s-glibc-C.UTF-8.txt`, `s-glibc-C.txt` | `ctype-summary.py < d-glibc-*.txt` | ranges and a sha256 per property; regenerating them from the dumps is byte-identical |
| `tr-glibc-C.txt` | `tr-glibc-C` | the bytes glibc's C locale writes on a wide stream for each cp >= 0x80 that does not write `?` (1784 entries, 150 of them empty) |

Code points above 0x10ffff (0x110000, 0x7fffffff, 0xffffffff, measured) have no class, map to
themselves and have width -1; the lookups in `libc/locale/ctype-utf8.cc` return the same without a
table entry.

To regenerate, on a host with glibc and its C.UTF-8 locale:

    cc -O2 -o gen-ctype-oracle gen-ctype-oracle.c
    ./gen-ctype-oracle C.UTF-8 > d-glibc-C.UTF-8.txt
    ./ctype-summary.py < d-glibc-C.UTF-8.txt | cmp - s-glibc-C.UTF-8.txt
    cc -O2 -o tr-glibc-C tr-glibc-C.c && ./tr-glibc-C > tr-glibc-C.txt
    ./gen-ctype-tables.py d-glibc-C.UTF-8.txt > ../../libc/locale/ctype-utf8.inc
    ./gen-translit.py tr-glibc-C.txt > ../../libc/locale/translit-c.inc

`check-tables.cc` proves the generated tables reproduce the measurements: built with
`libc/locale/ctype-utf8.cc`, `./check-tables ctype` prints the dump format and must have the sha256
above, and `./check-tables translit` must equal `tr-glibc-C.txt` without its `ferror=0` line. A
changed glibc gives a different dump; compare with `ctype-summary.py --compare A B`.

The same dump program built against musl (statically, musl 1.2.6) and run in C.UTF-8, compared
with `ctype-summary.py --compare`, gives the code points where musl's tables differ from glibc's:
alnum and alpha 19038, blank 13, cntrl 3, digit 0, graph 819503, lower 1191, print 819502, punct
139321, space 2, upper 645, xdigit 0, towupper 97, towlower 96, wcwidth 822617. Most come from
unassigned code points, which glibc treats as non-printable with width -1 and musl prints. musl 1.2.1
equals 1.2.6 except for wcwidth (240 code points), and musl's C locale equals its C.UTF-8.

`gen-ctype-sample.py ../../libc/locale/ctype-utf8.inc d-glibc-C.txt d-glibc-C.UTF-8.txt >
../../tests/tst-locale-ctype-sample.h` writes the expected values tst-locale-ctype checks: 0..0x2ff,
every run boundary of the generated tables, and 0x110000, 0x7fffffff and WEOF, in C and C.UTF-8.

## Named locales (conf_locale=1)

`libc/locale/locale-data.cc` reads the category files and the locale archive glibc's `localedef`
writes. It was written from a format specification of the files and from running programs against
glibc 2.41 (Debian 13); no glibc source was read.

- `make-test-locales.sh <out>` compiles the test locales with the build host's `localedef`:
  en_US en_GB de_DE fr_FR es_ES it_IT pt_BR nl_NL sv_SE pl_PL ru_RU ja_JP zh_CN ko_KR as
  `<out>/dir/<l>.utf8` and as an archive, plus a Latin-1 locale and da_DK.utf8 for the default
  directory. modules/tests puts them in the test image under `/usr/share/osv-locale-test` and
  `/usr/lib/locale`.
- `tests/misc-locale-dump.cc <name>` prints every langinfo item (its own list: the installed
  `<langinfo.h>` enum with the measured types), `localeconv()`, `strftime`
  of 38 conversions on 3 dates and `strptime` round trips of `%c %x %X`. Run it on Linux with
  `LOCPATH=<out>/dir` (and with the archive installed as `/usr/lib/locale/locale-archive`), and on
  OSv with `--env=LOCPATH=/usr/share/osv-locale-test/dir` (and without, for the archive). Compare
  the `@ ` lines.
- `tests/tst-locale-data.cc` passes on glibc with `OSV_LOCALE_TEST_DATA=<out>`, and on OSv.
- `fuzz-locale-data.cc` fuzzes the loader on Linux under ASan and UBSan, and feeds every accepted
  LC_TIME, LC_MONETARY and LC_MESSAGES to OSv's strftime, wcsftime, strptime, strfmon, localeconv's
  reads and rpmatch (`time-diff/build.sh` builds it; usage in the file).
- `item-types/item-types.sh <out> <scratch>` re-derives the item tables of `locale-data.cc`
  (`item_types`, `min_items`, `codeset_item`) by measurement: `item-dump.c` records what
  `nl_langinfo_l()` returns for every item of C, C.UTF-8, the test locales and every UTF-8 locale
  the host's localedef compiles (a word, or a pointer and the bytes there), `item-min.sh` the fewest
  items glibc accepts, `item-names.sh` the item names of the installed `<langinfo.h>`; then
  `item-types.py` classifies them, prints the type strings, and fails if they differ from
  `locale-data.cc` or from `langinfo-builtin.inc`'s types for C and C.UTF-8.

## Time, money, rpmatch, lookup fallback

Written from experiments with glibc 2.41 and the bytes of its compiled files; no glibc source read.
Each part has a host differential that builds the OSv source for Linux and compares it with glibc
over the same compiled data (`make-test-locales.sh` output, `LOCPATH=<out>/dir`), and a negative
control (a mutant that the differential must catch):

- `time-diff/build.sh` builds `libc/time/strftime.c`, `strptime.c`, `libc/locale/strfmon.c` and
  `rpmatch.c` for Linux with the shims in `time-diff/shim/` (their public names prefixed `osv_`);
  the harnesses call glibc directly. It takes the files from the tree it is in (`OSV_TREE`
  overrides).
  - `sfd d <locale>...` (`sfdiff.c`): strftime and wcsftime, every conversion character 32..126 with
    17 flag sets, 9 widths and the E/O modifiers on 12 dates, multi-conversion formats, buffer sizes
    0..11, and conversion characters U+0080..U+10FFFF: 455196 cases per locale.
  - `spd <locale>...` (`spdiff.c`): strptime round trips of every conversion's glibc output, with and
    without flags and E/O, and a fixed list of edge inputs, into a zeroed and a preset tm.
  - `fmd <locale>...` (`fmdiff.c`): strfmon_l, 16 flag sets x 6 widths x 5 left x 5 right
    precisions x n/i on 14 values, error formats, buffer sizes 0..39, and 1e308 (311 digits) at
    buffer sizes around its length.
- `time-diff/fbdiff.cc`: which locale glibc's `newlocale()` and OSv's `osv_locale_load()` find for a
  name (the directory fallback that drops the modifier and the territory).

glibc behaviours that were measured and reproduced (each surprising enough to note): wcsftime's
`%Z` ignores `#` and, with `-`, its width; a `%EY` pad flag sticks to later era years in the same
call; strptime's names and formats keep a "locale or C" state decided by the first differing match;
after one `%O` number falls back to ASCII digits every later `%O` fails; strfmon counts the field
width in bytes, and with `+` `%i` uses the national sign position.

Differences from glibc 2.41 that remain:
- A locale that is not UTF-8 is ENOENT on OSv; glibc loads it (`xx_L1@euro` in fbdiff).
- `nl_langinfo` of an LC_CTYPE word item: glibc returns the 32-bit word with the next 4 file bytes in
  the upper half; OSv returns the word alone.
- Collation is byte order. Transliteration in named locales is not supported.
- A loaded locale's LC_CTYPE answers from C.UTF-8's tables (classes, case maps, widths).
- strfmon with a locale string longer than glibc's internal limit: glibc truncates the field (a
  3000-byte `mon_decimal_point` gives 198 bytes), OSv returns the whole amount or E2BIG.
- `localeconv()->decimal_point` follows LC_NUMERIC, while printf and strtod still use ".".
- glibc's gmtime sets tm_zone "GMT", musl's "UTC" (misc-locale-dump uses localtime with TZ=UTC0).
