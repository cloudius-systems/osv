/*
 * Copyright (c) 2006-2007, Kohsuke Ohtani
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _RAMFS_H
#define _RAMFS_H

#include <osv/prex.h>

/* #define DEBUG_RAMFS 1 */

#ifdef DEBUG_RAMFS
#define DPRINTF(a)	dprintf a
#else
#define DPRINTF(a)	do {} while (0)
#endif

#define ASSERT(e)	assert(e)

/*
 * File/directory node for RAMFS
 */
struct ramfs_node {
	struct	ramfs_node *rn_next;   /* next node in the same directory */
	struct	ramfs_node *rn_child;  /* first child node */
	int	 rn_type;	/* file or directory */
	char	*rn_name;	/* name (null-terminated) */
	size_t	 rn_namelen;	/* length of name not including terminator */
	size_t	 rn_size;	/* file size */
	char	*rn_buf;	/* buffer to the file data */
	size_t	 rn_bufsize;	/* allocated buffer size */
};

__BEGIN_DECLS
struct ramfs_node *ramfs_allocate_node(char *name, int type);
void ramfs_free_node(struct ramfs_node *node);
__END_DECLS

#endif /* !_RAMFS_H */
