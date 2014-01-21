/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef STUBBING_HH_
#define STUBBING_HH_

#include <osv/debug.hh>

#define DO_ONCE(thing) do {				\
	static bool _x;					\
	if (!_x) {					\
	    _x = true;					\
	    thing ;					\
	}						\
} while (0)

#define WARN(msg) debug("WARNING: " msg)
#define WARN_ONCE(msg) DO_ONCE(WARN(msg))

#define UNIMPLEMENTED(msg) do {				\
	WARN("unimplemented " msg "\n");		\
	abort();					\
    } while (0)

#define NO_SYS(decl) decl {				\
    DO_ONCE(debug("%s not implemented\n", __func__));	\
    errno = ENOSYS;					\
    return -1;						\
}

#define UNIMPL(decl) decl { UNIMPLEMENTED(#decl); }

#define WARN_STUBBED() DO_ONCE(debug("%s() stubbed\n", __func__))

#endif
