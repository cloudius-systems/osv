/* Copyright (C) 2026 Greg Burd. This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 * Included first by build.sh into every OSv file built for Linux. The public names are renamed
 * (osv_...) in rename.h, after the system headers, so glibc's own declarations stay intact. */
#define _GNU_SOURCE 1
#ifndef OSV_CONF_LOCALE
#define OSV_CONF_LOCALE 1
#endif
#define hidden __attribute__((visibility("hidden")))
#define weak_alias(old, new) weak_alias_(old, new)
#define weak_alias_(old, new) extern __typeof(old) new __attribute__((weak, alias(#old)))
/* OSv's __nl_langinfo_l takes a null or LC_GLOBAL_LOCALE locale as the current one. */
#define __nl_langinfo_l osv_shim_nl_langinfo_l
