/* Copyright (C) 2026 Greg Burd. This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory. */
/* Linux shim for scripts/locale-oracle/time-diff (see build.sh). */
#include <time.h>
#define __tm_to_tzname(tm) ((tm)->tm_zone ? (tm)->tm_zone : "")
