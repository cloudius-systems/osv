/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * musl 1.2.4+ calls the internal __fstat() from __map_file.c (and declares it
 * `hidden` in src/include/sys/stat.h).  OSv has no __fstat of its own.
 *
 * The obvious workaround, -D__fstat=fstat on that one object, is a TRAP: the
 * define rewrites musl's DECLARATION as well as the call, so the translation
 * unit declares OSv's own fstat as hidden, and a hidden declaration anywhere
 * demotes the symbol to STB_LOCAL for the whole kernel.  Since OSv's runtime
 * symbol resolver only matches GLOBAL/WEAK, that silently broke dynamic
 * resolution of fstat for every glibc-linked shared object -- which is what made
 * conf_zfs=openzfs unbootable with
 *     /libzutil.so: failed looking up symbol fstat
 *
 * So define __fstat for real.  fstat() itself is just __fxstat(1, ...), so this
 * adds no behaviour; it only gives musl the name it expects, without renaming
 * anything and therefore without leaking visibility onto an exported symbol.
 */

#include <sys/stat.h>

int __fxstat(int ver, int fd, struct stat *st);

int __fstat(int fd, struct stat *st)
{
	return __fxstat(1, fd, st);
}
