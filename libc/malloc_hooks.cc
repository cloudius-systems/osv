/*
 * Copyright (C) 2015 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Stub implementation of the hooks in malloc_hooks(3):
//
// These hooks are global variables, which an application may set and our
// malloc() and friends are requested to call if the hook is != nullptr,
// instead of doing their usual work.
//
// malloc_hooks(3) clearly marks these variables as "not safe in
// multithreaded programs" and "deprecated", so we do *not* provide real
// support for them: malloc() et al. will *not* call them, even if set by
// an application. However, the existence of these variables helps run
// applications where some unused debugging code path sets these hooks.
// The problem is that variable name resolution happens at object load
// time (unlike function name resolution which only happens when the
// function is first used), so if the object just contains a reference to
// these hooks - even if the code setting them is never actually reached -
// we need these variables to exist.
// OpenFOAM is an example of an application that needs this.

#include <stddef.h>

extern "C" {
void *(*__malloc_hook)(size_t, const void *);
void *(*__realloc_hook)(void *, size_t, const void *);
void *(*__memalign_hook)(size_t, size_t, const void *);
void (*__free_hook)(void *, const void *);
void (*__after_morecore_hook)(void);
}
