/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * musl 1.2.4 stopped exporting the LFS64 SYMBOL aliases (fopen64,
 * mkstemp64, glob64, ...) and left only macros, now visible only under
 * _LARGEFILE64_SOURCE.  Code compiled against our own headers sees the
 * macros, but binaries built against glibc import the real symbols:
 *
 *  - the host libstdc++.a we link into the kernel references fseeko64 and
 *    ftello64 (__gnu_cxx::stdio_sync_filebuf);
 *  - any glibc-built application or library compiled with
 *    _FILE_OFFSET_BITS=64 (the autoconf AC_SYS_LARGEFILE default) has
 *    fopen, mkstemp, glob, ... redirected to the 64-suffixed names, even on
 *    a 64-bit target.
 *
 * All of these are listed in exported_symbols/osv_libc.so.6.symbols and
 * osv_ld-musl.so.1.symbols, and musl 1.2.1 defined every one of them, so
 * without these definitions OSv's dynamic linker fails such objects with
 * "failed looking up symbol fopen64".  The remaining names on that list
 * (open64, lseek64, pread64, stat64, readdir64, ...) are still defined by
 * OSv's own libc via LFS64() and need nothing here.
 *
 * On a 64-bit target off_t, fpos_t, struct dirent, struct stat and glob_t
 * are already the large-file types, byte for byte the same layout as
 * glibc's fpos64_t, struct dirent64, struct stat64 and glob64_t on both
 * x86_64 and aarch64, so each definition is exactly its unsuffixed form.
 * This adds no behaviour, only the symbol names.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <glob.h>
#include <ftw.h>

#undef fseeko64
#undef ftello64
#undef fopen64
#undef freopen64
#undef tmpfile64
#undef fgetpos64
#undef fsetpos64
#undef mkstemp64
#undef mkostemp64
#undef mkostemps64
#undef glob64
#undef globfree64
#undef nftw64
#undef scandir64
#undef alphasort64

int fseeko64(FILE *f, off_t off, int whence)
{
	return fseeko(f, off, whence);
}

off_t ftello64(FILE *f)
{
	return ftello(f);
}

FILE *fopen64(const char *restrict path, const char *restrict mode)
{
	return fopen(path, mode);
}

FILE *freopen64(const char *restrict path, const char *restrict mode,
	FILE *restrict f)
{
	return freopen(path, mode, f);
}

FILE *tmpfile64(void)
{
	return tmpfile();
}

int fgetpos64(FILE *restrict f, fpos_t *restrict pos)
{
	return fgetpos(f, pos);
}

int fsetpos64(FILE *f, const fpos_t *pos)
{
	return fsetpos(f, pos);
}

int mkstemp64(char *template)
{
	return mkstemp(template);
}

int mkostemp64(char *template, int flags)
{
	return mkostemp(template, flags);
}

int mkostemps64(char *template, int len, int flags)
{
	return mkostemps(template, len, flags);
}

int glob64(const char *restrict pat, int flags,
	int (*errfunc)(const char *, int), glob_t *restrict g)
{
	return glob(pat, flags, errfunc, g);
}

void globfree64(glob_t *g)
{
	globfree(g);
}

int nftw64(const char *path,
	int (*fn)(const char *, const struct stat *, int, struct FTW *),
	int fd_limit, int flags)
{
	return nftw(path, fn, fd_limit, flags);
}

int scandir64(const char *path, struct dirent ***res,
	int (*sel)(const struct dirent *),
	int (*cmp)(const struct dirent **, const struct dirent **))
{
	return scandir(path, res, sel, cmp);
}

int alphasort64(const struct dirent **a, const struct dirent **b)
{
	return alphasort(a, b);
}
