/*
 * Copyright (c) 2005-2008, Kohsuke Ohtani
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

#include <osv/prex.h>
#include <osv/vnode.h>
#include <osv/file.h>
#include <osv/mount.h>
#include <osv/buf.h>
#include <osv/debug.h>

#include <sys/stat.h>

#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>

#include "fatfs.h"

static int fatfs_mount	(mount_t mp, char *dev, int flags, void *data);
static int fatfs_unmount(mount_t mp);
#define fatfs_sync	((vfsop_sync_t)vfs_nullop)
static int fatfs_vget	(mount_t mp, struct vnode *vp);
#define fatfs_statfs	((vfsop_statfs_t)vfs_nullop)

/*
 * File system operations
 */
struct vfsops fatfs_vfsops = {
	fatfs_mount,		/* mount */
	fatfs_unmount,		/* unmount */
	fatfs_sync,		/* sync */
	fatfs_vget,		/* vget */
	fatfs_statfs,		/* statfs */
	&fatfs_vnops,		/* vnops */
};

/*
 * Read BIOS parameter block.
 * Return 0 on sucess.
 */
static int
fat_read_bpb(struct fatfsmount *fmp)
{
	struct buf *bp;
	struct fat_bpb *bpb;
	size_t size;
	int error;

	/* Read boot sector (block:0) */
	size = SEC_SIZE;
	error = bread(fmp->dev, 0, &bp);
	if (error) {
		kprintf("fatfs: failed to read boot sector\n");
		goto out;
	}

	bpb = bp->b_data;

	error = EINVAL;
	if (bpb->bytes_per_sector != SEC_SIZE) {
		kprintf("fatfs: invalid sector size\n");
		goto out_brelse;
	}

	/* Build FAT mount data */
	fmp->fat_start = bpb->hidden_sectors + bpb->reserved_sectors;
	fmp->root_start = fmp->fat_start +
		(bpb->num_of_fats * bpb->sectors_per_fat);
	fmp->data_start =
		fmp->root_start + (bpb->root_entries / DIR_PER_SEC);
	fmp->sec_per_cl = bpb->sectors_per_cluster;
	fmp->cluster_size = bpb->sectors_per_cluster * SEC_SIZE;
	fmp->last_cluster = (bpb->total_sectors - fmp->data_start) /
		bpb->sectors_per_cluster + CL_FIRST;
	fmp->free_scan = CL_FIRST;

	if (!strncmp((const char *)bpb->file_sys_id, "FAT12   ", 8)) {
		fmp->fat_type = 12;
		fmp->fat_mask = FAT12_MASK;
		fmp->fat_eof = CL_EOF & FAT12_MASK;
	} else if (!strncmp((const char *)bpb->file_sys_id, "FAT16   ", 8)) {
		fmp->fat_type = 16;
		fmp->fat_mask = FAT16_MASK;
		fmp->fat_eof = CL_EOF & FAT16_MASK;
	} else {
		/* FAT32 is not supported now! */
		kprintf("fatfs: invalid FAT type\n");
		error = EINVAL;
		goto out_brelse;
	}

	DPRINTF(("----- FAT info -----\n"));
	DPRINTF(("drive:%x\n", (int)bpb->physical_drive));
	DPRINTF(("total_sectors:%d\n", (int)bpb->total_sectors));
	DPRINTF(("heads       :%d\n", (int)bpb->heads));
	DPRINTF(("serial      :%x\n", (int)bpb->serial_no));
	DPRINTF(("cluster size:%u sectors\n", (int)fmp->sec_per_cl));
	DPRINTF(("fat_type    :FAT%u\n", (int)fmp->fat_type));
	DPRINTF(("fat_eof     :0x%x\n\n", (int)fmp->fat_eof));

	error = 0;
out_brelse:
	brelse(bp);
out:
	return error;
}

/*
 * Mount file system.
 */
static int
fatfs_mount(mount_t mp, char *dev, int flags, void *data)
{
	struct fatfs_node *np;
	struct fatfsmount *fmp;
	struct vnode *vp;
	int error = 0;

	DPRINTF(("fatfs_mount device=%s\n", dev));

	fmp = malloc(sizeof(struct fatfsmount));
	if (fmp == NULL)
		return ENOMEM;

	fmp->dev = mp->m_dev;
	error = fat_read_bpb(fmp);
	if (error)
		goto err1;

	error = ENOMEM;
	fmp->io_buf = malloc(fmp->sec_per_cl * SEC_SIZE);
	if (fmp->io_buf == NULL)
		goto err1;

	fmp->fat_buf = malloc(SEC_SIZE * 2);
	if (fmp->fat_buf == NULL)
		goto err2;

	fmp->dir_buf = malloc(SEC_SIZE);
	if (fmp->dir_buf == NULL)
		goto err3;

	mutex_init(&fmp->lock);
	mp->m_data = fmp;
	vp = mp->m_root;
	np = vp->v_data;
	np->blkno = CL_ROOT;
	return 0;
 err3:
	free(fmp->fat_buf);
 err2:
	free(fmp->io_buf);
 err1:
	free(fmp);
	return error;
}

/*
 * Unmount the file system.
 */
static int
fatfs_unmount(mount_t mp)
{
	struct fatfsmount *fmp;

	fmp = mp->m_data;
	free(fmp->dir_buf);
	free(fmp->fat_buf);
	free(fmp->io_buf);
	mutex_destroy(&fmp->lock);
	free(fmp);
	return 0;
}

/*
 * Prepare the FAT specific node and fill the vnode.
 */
static int
fatfs_vget(mount_t mp, struct vnode *vp)
{
	struct fatfs_node *np;

	np = malloc(sizeof(struct fatfs_node));
	if (np == NULL)
		return ENOMEM;
	vp->v_data = np;
	return 0;
}

