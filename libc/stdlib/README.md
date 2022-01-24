`qsort_r.c` - never implemented on musl side, should stay as is

## 3 others left to upgrade
* strtod.c
* strtol.c
* wcstol.c

`f.lock = -1;` replaced with `f.no_locking = true;` in respect to older musl 0.9.12
