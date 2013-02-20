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
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include "fatfs.h"

/*
 * Read the FAT entry for specified cluster.
 */
static int
read_fat_entry(struct fatfsmount *fmp, u_long cl)
{
	u_long sec;
	char *buf = fmp->fat_buf;
	int error, border = 0;
	struct buf *bp;

	/* Get the sector number in FAT entry. */
	if (FAT16(fmp))
		sec = (cl * 2) / SEC_SIZE;
	else {
		sec = (cl * 3 / 2) / SEC_SIZE;
		/*
		 * Check if the entry data is placed at the
		 * end of sector. If so, we have to read one
		 * more sector to get complete FAT12 entry.
		 */
		if ((cl * 3 / 2) % SEC_SIZE == SEC_SIZE - 1)
			border = 1;
	}
	sec += fmp->fat_start;

	/* Read first sector. */
	if ((error = bread(fmp->dev, sec, &bp)) != 0)
		return error;
	memcpy(buf, bp->b_data, SEC_SIZE);
	brelse(bp);

	if (!FAT12(fmp) || border == 0)
		return 0;

	/* Read second sector for the border entry of FAT12. */
	if ((error = bread(fmp->dev, sec + 1, &bp)) != 0)
		return error;
	memcpy(buf + SEC_SIZE, bp->b_data, SEC_SIZE);
	brelse(bp);
	return 0;
}

/*
 * Write fat entry from buffer.
 */
static int
write_fat_entry(struct fatfsmount *fmp, u_long cl)
{
	u_long sec;
	char *buf = fmp->fat_buf;
	int error, border = 0;
	struct buf *bp;

	/* Get the sector number in FAT entry. */
	if (FAT16(fmp))
		sec = (cl * 2) / SEC_SIZE;
	else {
		sec = (cl * 3 / 2) / SEC_SIZE;
		/* Check if border entry for FAT12 */
		if ((cl * 3 / 2) % SEC_SIZE == SEC_SIZE - 1)
			border = 1;
	}
	sec += fmp->fat_start;

	/* Write first sector. */
	bp = getblk(fmp->dev, sec);
	memcpy(bp->b_data, buf, SEC_SIZE);
	if ((error = bwrite(bp)) != 0)
		return error;

	if (!FAT12(fmp) || border == 0)
		return 0;

	/* Write second sector for the border entry of FAT12. */
	bp = getblk(fmp->dev, sec + 1);
	memcpy(bp->b_data, buf + SEC_SIZE, SEC_SIZE);
	error = bwrite(bp);
	return error;
}

/*
 * Get next cluster number of FAT chain.
 * @fmp: fat mount data
 * @cl: previous cluster#
 * @next: next cluster# to return
 */
int
fat_next_cluster(struct fatfsmount *fmp, u_long cl, u_long *next)
{
	u_int offset;
	uint16_t val;
	int error;

	/* Read FAT entry */
	error = read_fat_entry(fmp, cl);
	if (error)
		return error;

	/* Get offset in buffer. */
	if (FAT16(fmp))
		offset = (cl * 2) % SEC_SIZE;
	else
		offset = (cl * 3 / 2) % SEC_SIZE;

	/* Pick up cluster# */
	val = *((uint16_t *)(fmp->fat_buf + offset));

	/* Adjust data for FAT12 entry */
	if (FAT12(fmp)) {
		if (cl & 1)
			val >>= 4;
		else
			val &= 0xfff;
	}
	*next = (u_long)val;
	DPRINTF(("fat_next_cluster: %d => %d\n", cl, *next));
	return 0;
}

/*
 * Set next cluster number in FAT chain.
 * @fmp: fat mount data
 * @cl: previous cluster#
 * @next: cluster# to set (can be eof)
 */
int
fat_set_cluster(struct fatfsmount *fmp, u_long cl, u_long next)
{
	u_int offset;
	char *buf = fmp->fat_buf;
	int error;
	uint16_t val, tmp;

	/* Read FAT entry */
	error = read_fat_entry(fmp, cl);
	if (error)
		return error;

	/* Get offset in buffer. */
	if (FAT16(fmp))
		offset = (cl * 2) % SEC_SIZE;
	else
		offset = (cl * 3 / 2) % SEC_SIZE;

	/* Modify FAT entry for target cluster. */
	val = (uint16_t)(next & fmp->fat_mask);
	if (FAT12(fmp)) {
		tmp = *((uint16_t *)(buf + offset));
		if (cl & 1) {
			val <<= 4;
			val |= (tmp & 0xf);
		} else {
			tmp &= 0xf000;
			val |= tmp;
		}
	}
	*((uint16_t *)(buf + offset)) = val;

	/* Write FAT entry */
	error = write_fat_entry(fmp, cl);
	return error;
}

/*
 * Allocate free cluster in FAT chain.
 *
 * @fmp: fat mount data
 * @scan_start: cluster# to scan first. If 0, use the previous used value.
 * @free: allocated cluster# to return
 */
int
fat_alloc_cluster(struct fatfsmount *fmp, u_long scan_start, u_long *free)
{
	u_long cl, next;
	int error;

	if (scan_start == 0)
		scan_start = fmp->free_scan;

	DPRINTF(("fat_alloc_cluster: start=%d\n", scan_start));

	cl = scan_start + 1;
	while (cl != scan_start) {
		error = fat_next_cluster(fmp, cl, &next);
		if (error)
			return error;
		if (next == CL_FREE) {	/* free ? */
			DPRINTF(("fat_alloc_cluster: free cluster=%d\n", cl));
			*free = cl;
			return 0;
		}
		if (++cl >= fmp->last_cluster)
			cl = CL_FIRST;
	}
	return ENOSPC;		/* no space */
}

/*
 * Deallocate needless cluster.
 * @fmp: fat mount data
 * @start: first cluster# of FAT chain
 */
int
fat_free_clusters(struct fatfsmount *fmp, u_long start)
{
	int error;
	u_long cl, next;

	cl = start;
	if (cl < CL_FIRST)
		return EINVAL;

	while (!IS_EOFCL(fmp, cl)) {
		error = fat_next_cluster(fmp, cl, &next);
		if (error)
			return error;
		error = fat_set_cluster(fmp, cl, CL_FREE);
		if (error)
			return error;
		cl = next;
	}
	/* Clear eof */
	error = fat_set_cluster(fmp, cl, CL_FREE);
	if (error)
		return error;
	return 0;
}

/*
 * Get the cluster# for the specific file offset.
 *
 * @fmp: fat mount data
 * @start: start cluster# of file.
 * @offset: file offset
 * @cl: cluster# to return
 */
int
fat_seek_cluster(struct fatfsmount *fmp, u_long start, u_long offset,
		 u_long *cl)
{
	int error, i;
	u_long c, target;

	if (start > fmp->last_cluster)
		return EIO;

	c = start;
	target = offset / fmp->cluster_size;
	for (i = 0; i < target; i++) {
		error = fat_next_cluster(fmp, c, &c);
		if (error)
			return error;
		if (IS_EOFCL(fmp, c))
			return EIO;
	}
	*cl = c;
	return 0;
}

/*
 * Expand file size.
 *
 * @fmp: fat mount data
 * @cl: cluster# of target file.
 * @size: new size of file in bytes.
 */
int
fat_expand_file(struct fatfsmount *fmp, u_long cl, int size)
{
	int i, cl_len, alloc, error;
	u_long next;

	alloc = 0;
	cl_len = size / fmp->cluster_size + 1;

	for (i = 0; i < cl_len; i++) {
		error = fat_next_cluster(fmp, cl, &next);
		if (error)
			return error;
		if (alloc || next >= fmp->fat_eof) {
			error = fat_alloc_cluster(fmp, cl, &next);
			if (error)
				return error;
			alloc = 1;
		}
		if (alloc) {
			error = fat_set_cluster(fmp, cl, next);
			if (error)
				return error;
		}
		cl = next;
	}
	if (alloc)
		fat_set_cluster(fmp, cl, fmp->fat_eof);	/* add eof */
	DPRINTF(("fat_expand_file: new size=%d\n", size));
	return 0;
}

/*
 * Expand directory size.
 *
 * @fmp: fat mount data
 * @cl: cluster# of target directory
 * @new_cl: cluster# for new directory to return
 *
 * Note: The root directory can not be expanded.
 */
int
fat_expand_dir(struct fatfsmount *fmp, u_long cl, u_long *new_cl)
{
	int error;
	u_long next;

	/* Find last cluster number of FAT chain. */
	while (!IS_EOFCL(fmp, cl)) {
		error = fat_next_cluster(fmp, cl, &next);
		if (error)
			return error;
		cl = next;
	}

	error = fat_alloc_cluster(fmp, cl, &next);
	if (error)
		return error;

	error = fat_set_cluster(fmp, cl, next);
	if (error)
		return error;

	error = fat_set_cluster(fmp, next, fmp->fat_eof);
	if (error)
		return error;

	*new_cl = next;
	return 0;
}
