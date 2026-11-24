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

// Like backtrace_safe(), but when called from an interrupt handler it unwinds
// the INTERRUPTED thread rather than the handler itself: pc[0] is the saved rip
// from the exception frame and the rest of the walk follows the interrupted
// frame-pointer chain.
//
// Use this from anything that samples (a profiler, a periodic tracepoint).
// A frame-pointer walk can recover callers but not the interrupted leaf PC.
// Start with the saved PC, then walk the interrupted frame-pointer chain.
//
// Outside interrupt context this is exactly backtrace_safe().
int backtrace_safe_from_interrupt(void** pc, int nr);


#endif /* EXECINFO_HH_ */
