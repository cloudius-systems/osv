/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * musl 1.2.4 stopped exporting the LFS64 SYMBOL aliases (fseeko64,
 * ftello64, fopen64, ...) and left only macros in <stdio.h>; see its
 * 1.2.4 release note "the LFS64 macros are no longer exposed without
 * _LARGEFILE64_SOURCE".  That is fine for anything we compile, which
 * sees the macros, but the host libstdc++.a we link against was built
 * against a libc that exported the real symbols, so
 * __gnu_cxx::stdio_sync_filebuf carries undefined references to
 * fseeko64 and ftello64.
 *
 * These two definitions satisfy those references.  On a 64-bit target
 * off_t is already 64-bit, so each is exactly its unsuffixed form; this
 * adds no behaviour, only the symbol name.  Kept to the two names
 * libstdc++ actually needs rather than reinstating all ten musl
 * dropped, on the principle that an unused alias is an unused alias.
 */

#include <stdio.h>
#include <sys/types.h>

#undef fseeko64
#undef ftello64

int fseeko64(FILE *f, off_t off, int whence)
{
	return fseeko(f, off, whence);
}

off_t ftello64(FILE *f)
{
	return ftello(f);
}
