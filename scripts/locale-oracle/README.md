The values glibc 2.42 (x86_64) returns from `nl_langinfo_l(item, newlocale(LC_ALL_MASK, name, 0))`
for its C and C.UTF-8 locales, for every item of LC_NUMERIC, LC_TIME, LC_MONETARY, LC_MESSAGES,
LC_PAPER, LC_NAME, LC_ADDRESS, LC_TELEPHONE, LC_MEASUREMENT and LC_IDENTIFICATION.

Each row is `<item> <category> <index> <type> <value>`. Types: `s` string (hex bytes), `b` byte
item (hex, read up to a NUL, so only the first byte is the item), `d` word (decimal: glibc returns
the value in the pointer bits), `w` wide string (code points).

`scripts/gen-langinfo-builtin.py` turns these into `libc/locale/langinfo-builtin.inc`.
