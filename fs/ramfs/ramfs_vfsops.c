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

#include <errno.h>

#include <osv/vnode.h>
#include <osv/mount.h>
#include <osv/dentry.h>

#include "ramfs.h"

extern struct vnops ramfs_vnops;

static int ramfs_mount(struct mount *mp, char *dev, int flags, void *data);
static int ramfs_unmount(struct mount *mp, int flags);
#define ramfs_sync	((vfsop_sync_t)vfs_nullop)
#define ramfs_vget	((vfsop_vget_t)vfs_nullop)
#define ramfs_statfs	((vfsop_statfs_t)vfs_nullop)

/*
 * File system operations
 */
struct vfsops ramfs_vfsops = {
	ramfs_mount,		/* mount */
	ramfs_unmount,		/* unmount */
	ramfs_sync,		/* sync */
	ramfs_vget,		/* vget */
	ramfs_statfs,		/* statfs */
	&ramfs_vnops,		/* vnops */
};

/*
 * Mount a file system.
 */
static int
ramfs_mount(struct mount *mp, char *dev, int flags, void *data)
{
	struct ramfs_node *np;

	DPRINTF(("ramfs_mount: dev=%s\n", dev));

	/* Create a root node */
	np = ramfs_allocate_node("/", VDIR);
	if (np == NULL)
		return ENOMEM;
	mp->m_root->d_vnode->v_data = np;
	return 0;
}

/*
 * Unmount a file system.
 *
 * NOTE: Currently, we don't support unmounting of the RAMFS. This is
 *       because we have to deallocate all nodes included in all sub
 *       directories, and it requires more work...
 */
static int
ramfs_unmount(struct mount *mp, int flags)
{
	release_mp_dentries(mp);
	return 0;
}
