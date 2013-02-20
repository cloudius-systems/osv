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
#include <osv/buf.h>

#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>

#include "fatfs.h"

/*
 * Read directory entry to buffer, with cache.
 */
static int
fat_read_dirent(struct fatfsmount *fmp, u_long sec)
{
	struct buf *bp;
	int error;

	if ((error = bread(fmp->dev, sec, &bp)) != 0)
		return error;
	memcpy(fmp->dir_buf, bp->b_data, SEC_SIZE);
	brelse(bp);
	return 0;
}

/*
 * Write directory entry from buffer.
 */
static int
fat_write_dirent(struct fatfsmount *fmp, u_long sec)
{
	struct buf *bp;

	bp = getblk(fmp->dev, sec);
	memcpy(bp->b_data, fmp->dir_buf, SEC_SIZE);
	return bwrite(bp);
}

/*
 * Find directory entry in specified sector.
 * The fat vnode data is filled if success.
 *
 * @fmp: fatfs mount point
 * @sec: sector#
 * @name: file name
 * @node: pointer to fat node
 */
static int
fat_lookup_dirent(struct fatfsmount *fmp, u_long sec, char *name,
		  struct fatfs_node *np)
{
	struct fat_dirent *de;
	int error, i;

	error = fat_read_dirent(fmp, sec);
	if (error)
		return error;

	de = (struct fat_dirent *)fmp->dir_buf;

	for (i = 0; i < DIR_PER_SEC; i++) {
		/* Find specific file or directory name */
		if (IS_EMPTY(de))
			return ENOENT;
		if (!IS_VOL(de) &&
		    !fat_compare_name((char *)de->name, name)) {
			/* Found. Fill the fat vnode data. */
			*(&np->dirent) = *de;
			np->sector = sec;
			np->offset = sizeof(struct fat_dirent) * i;
			DPRINTF(("fat_lookup_dirent: found sec=%d\n", sec));
			return 0;
		}
		if (!IS_DELETED(de))
			DPRINTF(("fat_lookup_dirent: %s\n", de->name));
		de++;
	}
	return EAGAIN;
}

/*
 * Find directory entry for specified name in directory.
 * The fat vnode data is filled if success.
 *
 * @dvp: vnode for directory.
 * @name: file name
 * @np: pointer to fat node
 */
int
fatfs_lookup_node(vnode_t dvp, char *name, struct fatfs_node *np)
{
	struct fatfs_node *dp = dvp->v_data;
	struct fatfsmount *fmp;
	char fat_name[12];
	u_long cl, sec;
	int i, error;

	if (name == NULL)
		return ENOENT;

	DPRINTF(("fat_lookup_denode: cl=%d name=%s\n", dvp->v_blkno, name));

	fat_convert_name(name, fat_name);
	*(fat_name + 11) = '\0';

	fmp = (struct fatfsmount *)dvp->v_mount->m_data;
	cl = dp->blkno;
	if (cl == CL_ROOT) {
		/* Search entry in root directory */
		for (sec = fmp->root_start; sec < fmp->data_start; sec++) {
			error = fat_lookup_dirent(fmp, sec, fat_name, np);
			if (error != EAGAIN)
				return error;
		}
	} else {
		/* Search entry in sub directory */
		while (!IS_EOFCL(fmp, cl)) {
			sec = cl_to_sec(fmp, cl);
			for (i = 0; i < fmp->sec_per_cl; i++) {
				error = fat_lookup_dirent(fmp, sec, fat_name,
						   np);
				if (error != EAGAIN)
					return error;
				sec++;
			}
			error = fat_next_cluster(fmp, cl, &cl);
			if (error)
				return error;
		}
	}
	return ENOENT;
}

/*
 * Get directory entry for specified index in sector.
 * The directory entry is filled if success.
 *
 * @fmp: fatfs mount point
 * @sec: sector#
 * @target: target index
 * @index: current index
 * @np: pointer to fat node
 */
static int
fat_get_dirent(struct fatfsmount *fmp, u_long sec, int target, int *index,
	       struct fatfs_node *np)
{
	struct fat_dirent *de;
	int error, i;

	error = fat_read_dirent(fmp, sec);
	if (error)
		return error;

	de = (struct fat_dirent *)fmp->dir_buf;
	for (i = 0; i < DIR_PER_SEC; i++) {
		if (IS_EMPTY(de))
			return ENOENT;
		if (!IS_DELETED(de) && !IS_VOL(de)) {
			/* valid file */
			if (*index == target) {
				*(&np->dirent) = *de;
				np->sector = sec;
				np->offset = sizeof(struct fat_dirent) * i;
				DPRINTF(("fat_get_dirent: found index=%d\n", *index));
				return 0;
			}
			(*index)++;
		}
		DPRINTF(("fat_get_dirent: %s\n", de->name));
		de++;
	}
	return EAGAIN;
}

/*
 * Get directory entry for specified index.
 *
 * @dvp: vnode for directory.
 * @index: index of the entry
 * @np: pointer to fat node
 */
int
fatfs_get_node(vnode_t dvp, int index, struct fatfs_node *np)
{
	struct fatfs_node *dp = dvp->v_data;
	struct fatfsmount *fmp;
	u_long cl, sec;
	int i, cur_index, error;

	fmp = (struct fatfsmount *)dvp->v_mount->m_data;
	cl = dp->blkno;
	cur_index = 0;

	DPRINTF(("fatfs_get_node: index=%d\n", index));

	if (cl == CL_ROOT) {
		/* Get entry from the root directory */
		for (sec = fmp->root_start; sec < fmp->data_start; sec++) {
			error = fat_get_dirent(fmp, sec, index, &cur_index, np);
			if (error != EAGAIN)
				return error;
		}
	} else {
		/* Get entry from the sub directory */
		while (!IS_EOFCL(fmp, cl)) {
			sec = cl_to_sec(fmp, cl);
			for (i = 0; i < fmp->sec_per_cl; i++) {
				error = fat_get_dirent(fmp, sec, index,
						     &cur_index, np);
				if (error != EAGAIN)
					return error;
				sec++;
			}
			error = fat_next_cluster(fmp, cl, &cl);
			if (error)
				return error;
		}
	}
	return ENOENT;
}

/*
 * Find empty directory entry and put new entry on it.
 *
 * @fmp: fatfs mount point
 * @sec: sector#
 * @np: pointer to fat node
 */
static int
fat_add_dirent(struct fatfsmount *fmp, u_long sec, struct fatfs_node *np)
{
	struct fat_dirent *de;
	int error, i;

	error = fat_read_dirent(fmp, sec);
	if (error)
		return error;

	de = (struct fat_dirent *)fmp->dir_buf;
	for (i = 0; i < DIR_PER_SEC; i++) {
		if (IS_DELETED(de) || IS_EMPTY(de))
			goto found;
		DPRINTF(("fat_add_dirent: scan %s\n", de->name));
		de++;
	}
	return ENOENT;

 found:
	DPRINTF(("fat_add_dirent: found. sec=%d\n", sec));
	memcpy(de, &np->dirent, sizeof(struct fat_dirent));
	error = fat_write_dirent(fmp, sec);
	return error;
}

/*
 * Find empty directory entry and put new entry on it.
 * This search is done only in directory of specified cluster.
 * @dvp: vnode for directory.
 * @np: pointer to fat node
 */
int
fatfs_add_node(vnode_t dvp, struct fatfs_node *np)
{
	struct fatfsmount *fmp;
	struct fatfs_node *dp = dvp->v_data;
	u_long cl, sec;
	int i, error;
	u_long next;

	fmp = (struct fatfsmount *)dvp->v_mount->m_data;
	cl = dp->blkno;

	DPRINTF(("fatfs_add_node: cl=%d\n", cl));

	if (cl == CL_ROOT) {
		/* Add entry in root directory */
		for (sec = fmp->root_start; sec < fmp->data_start; sec++) {
			error = fat_add_dirent(fmp, sec, np);
			if (error != ENOENT)
				return error;
		}
	} else {
		/* Search entry in sub directory */
		while (!IS_EOFCL(fmp, cl)) {
			sec = cl_to_sec(fmp, cl);
			for (i = 0; i < fmp->sec_per_cl; i++) {
				error = fat_add_dirent(fmp, sec, np);
				if (error != ENOENT)
					return error;
				sec++;
			}
			error = fat_next_cluster(fmp, cl, &next);
			if (error)
				return error;
			cl = next;
		}
		/* No entry found, add one more free cluster for directory */
		DPRINTF(("fatfs_add_node: expand dir\n"));
		error = fat_expand_dir(fmp, cl, &next);
		if (error)
			return error;

		/* Initialize free cluster. */
		memset(fmp->dir_buf, 0, SEC_SIZE);
		sec = cl_to_sec(fmp, next);
		for (i = 0; i < fmp->sec_per_cl; i++) {
			error = fat_write_dirent(fmp, sec);
			if (error)
				return error;
			sec++;
		}
		/* Try again */
		sec = cl_to_sec(fmp, next);
		error = fat_add_dirent(fmp, sec, np);
		return error;
	}
	return ENOENT;
}

/*
 * Put directory entry.
 * @fmp: fat mount data
 * @np: pointer to fat node
 */
int
fatfs_put_node(struct fatfsmount *fmp, struct fatfs_node *np)
{
	int error;

	error = fat_read_dirent(fmp, np->sector);
	if (error)
		return error;

	memcpy(fmp->dir_buf + np->offset, &np->dirent,
	       sizeof(struct fat_dirent));

	error = fat_write_dirent(fmp, np->sector);
	return error;
}

