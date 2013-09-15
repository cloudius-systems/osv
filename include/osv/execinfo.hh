/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef EXECINFO_HH_
#define EXECINFO_HH_

// Similar to backtrace(), but works even with a corrupted stack.  Uses
// frame pointers instead of DWARF debug information, so it works in interrupt
// contexts, but requires -fno-omit-frame-pointer
int backtrace_safe(void** pc, int nr);


#endif /* EXECINFO_HH_ */
