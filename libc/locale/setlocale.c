/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <locale.h>
#include <string.h>

/*
 * OSv has no on-disk locale database and provides only the C locale.  The
 * historical stub returned "C.UTF-8" unconditionally and ignored its
 * arguments, which behaves incorrectly in two ways an application can observe:
 *
 *   - A set request for a locale OSv cannot provide (e.g. "en_US.UTF-8") must
 *     fail by returning NULL so the caller learns it is unavailable.  The stub
 *     silently "succeeded", reporting a locale that was never in effect.
 *
 *   - setlocale() must return a name that newlocale() will accept, because
 *     portable software round-trips the two -- PostgreSQL, for one, derives a
 *     new database's default collation from setlocale() and feeds that name
 *     back to newlocale().  The stub returned "C.UTF-8", which OSv's
 *     newlocale() rejects, so a PostgreSQL cluster initialized with
 *     LC_COLLATE=C nonetheless stamped every CREATE DATABASE with
 *     datcollate="C.UTF-8" and then could not open it ("could not create
 *     locale \"C.UTF-8\": ENOENT").  This blocked any application that creates
 *     its own database at runtime.
 *
 * Report the C locale (which newlocale() accepts) and accept only the
 * C-family names OSv can actually provide, failing others.  Server
 * character-set encoding (e.g. UTF8 in PostgreSQL) is independent of the libc
 * locale and is unaffected.
 */

static int c_family_locale(const char *name)
{
	return name[0] == '\0' ||
	       !strcmp(name, "C") ||
	       !strcmp(name, "POSIX") ||
	       !strcmp(name, "C.UTF-8");
}

char *setlocale(int category, const char *locale)
{
	if ((unsigned)category > LC_ALL)
		return 0;

	/* A set request for a locale OSv does not provide must fail. */
	if (locale && !c_family_locale(locale))
		return 0;

	/* The single locale OSv provides, named so newlocale() accepts it. */
	return (char *)"C";
}
