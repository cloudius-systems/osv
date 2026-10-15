/* Copyright (C) 2026 Greg Burd. This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 * Included first by build.sh: build OSv's libc/time and libc/locale C files on Linux, their
 * public names prefixed osv_. */
#define _GNU_SOURCE 1
#define __tm_gmtoff tm_gmtoff
#define __tm_zone tm_zone
#define __nl_langinfo_l osv_shim_nl_langinfo_l
#define strftime osv_strftime
#define strftime_l osv_strftime_l
#define __strftime_l osv___strftime_l
#define wcsftime osv_wcsftime
#define wcsftime_l osv_wcsftime_l
#define __wcsftime_l osv___wcsftime_l
#define strptime osv_strptime
#define strptime_l osv_strptime_l
#define __strptime_l osv___strptime_l
#define strfmon osv_strfmon
#define strfmon_l osv_strfmon_l
#define weak_alias(old, new) weak_alias_(old, new)
#define weak_alias_(old, new) extern __typeof(old) new __attribute__((weak, alias(#old)))
/* OSv's __nl_langinfo_l takes a null or LC_GLOBAL_LOCALE locale as the current one; glibc's does not. */
#include <langinfo.h>
char *osv_shim_nl_langinfo_l(nl_item, locale_t);
