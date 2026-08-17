// SPDX-License-Identifier: CDDL-1.0
/*
 * OSv Lua compatibility fixes for OpenZFS.
 *
 * This header is force-included for Lua compilation units to resolve
 * conflicts and ensure required headers are present.
 */

#ifndef _ZFS_LUA_FIX_H
#define	_ZFS_LUA_FIX_H

/*
 * Lua needs setjmp.h for error handling (longjmp/setjmp).
 */
#include <setjmp.h>

/*
 * Lua has a 'panic' struct member that conflicts with the panic() function macro.
 * Declare panic as a function so Lua code can use it.
 */
#ifdef panic
#undef panic
#endif
__attribute__((noreturn)) extern void panic(const char *, ...);


#endif /* _ZFS_LUA_FIX_H */
