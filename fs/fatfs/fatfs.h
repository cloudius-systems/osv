/*
 * Copyright (c) 2005-2007, Kohsuke Ohtani
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

#ifndef _FATFS_H
#define _FATFS_H

#include <sys/cdefs.h>
#include <sys/types.h>
#include <stdint.h>

#include <osv/prex.h>
#include <osv/vnode.h>
#include <osv/file.h>
#include <osv/mount.h>
#include <osv/buf.h>

/* #define DEBUG_FATFS 1 */

#ifdef DEBUG_FATFS
#define DPRINTF(a)	kprintf a
#else
#define DPRINTF(a)	do {} while (0)
#endif

#define ASSERT(e)	assert(e)


#define SEC_SIZE	512		/* sector size */
#define SEC_INVAL	0xffffffff	/* invalid sector */

/*
 * Pre-defined cluster number
 */
#define CL_ROOT		0		/* cluster 0 means the root directory */
#define CL_FREE		0		/* cluster 0 also means the free cluster */
#define CL_FIRST	2		/* first legal cluster */
#define CL_LAST		0xfffffff5	/* last legal cluster */
#define CL_EOF		0xffffffff	/* EOF cluster */

#define EOF_MASK	0xfffffff8	/* mask of eof */

#define FAT12_MASK	0x00000fff
#define FAT16_MASK	0x0000ffff

/*
 * BIOS parameter block
 */
struct fat_bpb {
	uint16_t	jmp_instruction;
	uint8_t		nop_instruction;
	uint8_t		oem_id[8];
	uint16_t	bytes_per_sector;
	uint8_t		sectors_per_cluster;
	uint16_t	reserved_sectors;
	uint8_t		num_of_fats;
	uint16_t	root_entries;
	uint16_t	total_sectors;
	uint8_t		media_descriptor;
	uint16_t	sectors_per_fat;
	uint16_t	sectors_per_track;
	uint16_t	heads;
	uint32_t	hidden_sectors;
	uint32_t	big_total_sectors;
	uint8_t		physical_drive;
	uint8_t		reserved;
	uint8_t		ext_boot_signature;
	uint32_t	serial_no;
	uint8_t		volume_id[11];
	uint8_t		file_sys_id[8];
} __packed;

/*
 * FAT directory entry
 */
struct fat_dirent {
	uint8_t		name[11];
	uint8_t		attr;
	uint8_t		reserve[10];
	uint16_t	time;
	uint16_t	date;
	uint16_t	cluster;
	uint32_t	size;
} __packed;

#define SLOT_EMPTY	0x00
#define SLOT_DELETED	0xe5

#define DIR_PER_SEC     (SEC_SIZE / sizeof(struct fat_dirent))

/*
 * FAT attribute for attr
 */
#define FA_RDONLY	0x01
#define FA_HIDDEN	0x02
#define FA_SYSTEM	0x04
#define FA_VOLID	0x08
#define FA_SUBDIR	0x10
#define FA_ARCH		0x20
#define FA_DEVICE	0x40

#define IS_DIR(de)	(((de)->attr) & FA_SUBDIR)
#define IS_VOL(de)	(((de)->attr) & FA_VOLID)
#define IS_FILE(de)	(!IS_DIR(de) && !IS_VOL(de))

#define IS_DELETED(de)  ((de)->name[0] == 0xe5)
#define IS_EMPTY(de)    ((de)->name[0] == 0)

/*
 * Mount data
 */
struct fatfsmount {
	int	fat_type;	/* 12 or 16 */
	u_long	root_start;	/* start sector for root directory */
	u_long	fat_start;	/* start sector for fat entries */
	u_long	data_start;	/* start sector for data */
	u_long	fat_eof;	/* id of end cluster */
	u_long	sec_per_cl;	/* sectors per cluster */
	u_long	cluster_size;	/* cluster size */
	u_long	last_cluster;	/* last cluser */
	u_long	fat_mask;	/* mask for cluster# */
	u_long	free_scan;	/* start cluster# to free search */
	struct vnode *root_vnode;	/* vnode for root */
	char	*io_buf;	/* local data buffer */
	char	*fat_buf;	/* buffer for fat entry */
	char	*dir_buf;	/* buffer for directory entry */
	struct device *dev;		/* mounted device */
	mutex_t lock;		/* file system lock */
};

#define FAT12(fat)	((fat)->fat_type == 12)
#define FAT16(fat)	((fat)->fat_type == 16)

#define IS_EOFCL(fat, cl) \
	(((cl) & EOF_MASK) == ((fat)->fat_mask & EOF_MASK))

typedef uint64_t daddr_t;

/*
 * File/directory node
 */
struct fatfs_node {
	struct fat_dirent dirent; /* copy of directory entry */
	u_long	sector;		/* sector# for directory entry */
	u_long	offset;		/* offset of directory entry in sector */
	daddr_t blkno;
};

extern struct vnops fatfs_vnops;

/* Macro to convert cluster# to logical sector# */
#define cl_to_sec(fat, cl) \
            (fat->data_start + (cl - 2) * fat->sec_per_cl)

__BEGIN_DECLS
int	 fat_next_cluster(struct fatfsmount *fmp, u_long cl, u_long *next);
int	 fat_set_cluster(struct fatfsmount *fmp, u_long cl, u_long next);
int	 fat_alloc_cluster(struct fatfsmount *fmp, u_long scan_start, u_long *free);
int	 fat_free_clusters(struct fatfsmount *fmp, u_long start);
int	 fat_seek_cluster(struct fatfsmount *fmp, u_long start, u_long offset,
			    u_long *cl);
int	 fat_expand_file(struct fatfsmount *fmp, u_long cl, int size);
int	 fat_expand_dir(struct fatfsmount *fmp, u_long cl, u_long *new_cl);

void	 fat_convert_name(char *org, char *name);
void	 fat_restore_name(char *org, char *name);
int	 fat_valid_name(char *name);
int	 fat_compare_name(char *n1, char *n2);
void	 fat_mode_to_attr(mode_t mode, u_char *attr);
void	 fat_attr_to_mode(u_char attr, mode_t *mode);

int	 fatfs_lookup_node(struct vnode *dvp, char *name, struct fatfs_node *node);
int	 fatfs_get_node(struct vnode *dvp, int index, struct fatfs_node *node);
int	 fatfs_put_node(struct fatfsmount *fmp, struct fatfs_node *node);
int	 fatfs_add_node(struct vnode *dvp, struct fatfs_node *node);
__END_DECLS

#endif /* !_FATFS_H */
