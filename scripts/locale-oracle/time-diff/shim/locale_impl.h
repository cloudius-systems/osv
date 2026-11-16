/* Copyright (C) 2026 Greg Burd. This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory. */
/* Linux shim for scripts/locale-oracle/time-diff (see build.sh). */
#include <locale.h>
#define CURRENT_LOCALE uselocale((locale_t)0)
locale_t __osv_c_locale(void);
#define C_LOCALE (__osv_c_locale())
