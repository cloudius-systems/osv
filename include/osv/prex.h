/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef _OSV_PREX_H
#define _OSV_PREX_H 1


#include <unistd.h>
#include <osv/fcntl.h>

__BEGIN_DECLS

#define __packed        __attribute__((__packed__))

#define	BSIZE	512		/* size of secondary block (bytes) */

#define DO_RDWR		0x2

#define PAGE_SIZE	4096
#define PAGE_MASK	(PAGE_SIZE-1)
#define round_page(x)	(((x) + PAGE_MASK) & ~PAGE_MASK)

size_t strlcat(char *dst, const char *src, size_t siz);
size_t strlcpy(char *dst, const char *src, size_t siz);

void sys_panic(const char *);

__END_DECLS

#endif /* _OSV_PREX_H */
