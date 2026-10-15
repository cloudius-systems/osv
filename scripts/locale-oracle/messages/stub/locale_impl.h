/* Copyright (C) 2026 Greg Burd. BSD-3-Clause (see the LICENSE file in the top-level directory).
 * Stub for osv-english.sh: musl's message sources built for Linux, untranslated. */
#include <locale.h>
#define LCTRANS(msg, lc, loc) (msg)
#define LCTRANS_CUR(msg) (msg)
#define CURRENT_LOCALE ((locale_t)0)
#define weak_alias(old, new) extern int osv_english_unused_##new
#define hidden
