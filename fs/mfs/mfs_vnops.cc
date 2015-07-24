/*
 * Copyright 2015 Carnegie Mellon University
 * This material is based upon work funded and supported by the Department of
 * Defense under Contract No. FA8721-05-C-0003 with Carnegie Mellon University
 * for the operation of the Software Engineering Institute, a federally funded
 * research and development center.
 * 
 * Any opinions, findings and conclusions or recommendations expressed in this
 * material are those of the author(s) and do not necessarily reflect the views
 * of the United States Department of Defense.
 * 
 * NO WARRANTY. THIS CARNEGIE MELLON UNIVERSITY AND SOFTWARE ENGINEERING
 * INSTITUTE MATERIAL IS FURNISHED ON AN "AS-IS" BASIS. CARNEGIE MELLON
 * UNIVERSITY MAKES NO WARRANTIES OF ANY KIND, EITHER EXPRESSED OR IMPLIED, AS
 * TO ANY MATTER INCLUDING, BUT NOT LIMITED TO, WARRANTY OF FITNESS FOR PURPOSE
 * OR MERCHANTABILITY, EXCLUSIVITY, OR RESULTS OBTAINED FROM USE OF THE
 * MATERIAL. CARNEGIE MELLON UNIVERSITY DOES NOT MAKE ANY WARRANTY OF ANY KIND
 * WITH RESPECT TO FREEDOM FROM PATENT, TRADEMARK, OR COPYRIGHT INFRINGEMENT.
 * 
 * This material has been approved for public release and unlimited
 * distribution.
 * 
 * DM-0002621
 */

#include <sys/stat.h>
#include <dirent.h>
#include <sys/param.h>

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>

#include <osv/prex.h>
#include <osv/vnode.h>
#include <osv/file.h>
#include <osv/mount.h>
#include <osv/debug.h>

#include <osv/buf.h>
#include <sys/types.h>
#include <osv/device.h>

#include "mfs.h"

static mutex_t mfs_lock = MUTEX_INITIALIZER;

// Used by extern declaration in fs/vfs/vfs_conf.cc
int mfs_init(void) {
    return 0;
}

static int mfs_open(struct file *fp) {
	if ((file_flags(fp) & FWRITE)) {
		// We do not allow writing! jerks
		return (EPERM);
	}
	return 0;
}

static int mfs_close(struct vnode *vp, struct file *fp) {
	print("[mfs] mfs_close called\n");
	// Nothing to do really...
	return 0;
}

static size_t min(size_t a, size_t b) {
	if (a > b) return b;
	return a;
}

static int mfs_read(struct vnode *vnode, struct file* fp, struct uio *uio, int ioflag) {
	print("[mfs] mfs_read called\n");

	struct mfs_inode       *inode  = (struct mfs_inode*)vnode->v_data;
	struct mfs_super_block *sb     = (struct mfs_super_block*)vnode->v_mount->m_data;
	struct device          *device = vnode->v_mount->m_dev;
	struct buf             *bh     = NULL;
	char                   *data   = NULL;

	//uio_offset = start read point
	//uio_resid  = read length

	size_t   len    =  0;
	int      rv     =  0;
	int      error  = -1;
	uint64_t block  =  inode->data_block_number;
	uint64_t offset =  0;
	// Total read amount is what they requested, or what is left
	uint64_t read_amt = min(inode->file_size - uio->uio_offset, uio->uio_resid);
    // uint64_t max_r  =  inode->file_size;
    uint64_t total  =  0;

    // printf("[mfs] Reading %llu bytes from %s with size %llu\n", read_amt, file_dentry(fp)->d_path, inode->file_size);

    print("[mfs] reading file with name: %s\n", file_dentry(fp)->d_path);
    print("[mfs] Reading file from inode %llu\n", inode->inode_no);
    print("[mfs] max_r = %llu\n", max_r);
    print("[mfs] offset: %lu\n", uio->uio_offset);

	// Calculate which block we need actually need to read
	block += uio->uio_offset / sb->block_size;
	offset = uio->uio_offset % sb->block_size;


    // printf("[mfs] starting block = %llu\n", block);
    // printf("[mfs] offset = %llu\n", offset);

	// Cant read directories
	if (vnode->v_type == VDIR)
		return EISDIR;
	// Cant read anything but reg
	if (vnode->v_type != VREG)
		return EINVAL;
	// Cant start reading before the first byte
	if (uio->uio_offset < 0)
		return EINVAL;
	// Need to read more than 1 byte
	if (uio->uio_resid == 0)
		return 0;
	// Cant read after the end of the file
	if (uio->uio_offset >= (off_t)vnode->v_size)
		return 0;

    while (read_amt > 0) {
		// Force the read to fit inside a block
		len = min(sb->block_size - offset, read_amt);
		// len = min(len, inode->file_size - total); // Needs work I think

		// print("[mfs] reading %lu bytes\n", len);

		error = bread(device, block, &bh);
		if (error) {
			kprintf("[mfs] Error reading block [%llu]\n", block);
        	return 0;
		}

		// total += len;
		data = (char *)bh->b_data;
		rv = uiomove(data + offset, len, uio);
	
		brelse(bh);

		// Move on to the next block
		// Set offset to 0 to make sure we start the start of the next block
		offset = 0;
		block++;
		read_amt -= len;
		total += len;
	}

	// printf("[mfs] read %llu bytes.\n", total);

	return rv;
}

static int mfs_readdir(struct vnode *vnode, struct file *fp, struct dirent *dir) {
	print("[mfs] mfs_readdir called\n");
	
	mutex_lock(&mfs_lock);

	struct mfs_inode       *inode  = (struct mfs_inode*)vnode->v_data;
	struct mfs_super_block *sb     = (struct mfs_super_block*)vnode->v_mount->m_data;
	struct device          *device = vnode->v_mount->m_dev;
	struct mfs_dir_record  *record = NULL;
	struct buf             *bh     = NULL;
	
	int      error  = -1;
	uint64_t index  =  0;
	uint64_t block  =  inode->data_block_number;
	uint64_t offset =  0;

	if (fp->f_offset == 0) {
		dir->d_type = DT_DIR;
		strlcpy((char *)&dir->d_name, ".", sizeof(dir->d_name));
	} else if (fp->f_offset == 1) {
		dir->d_type = DT_DIR;
		strlcpy((char *)&dir->d_name, "..", sizeof(dir->d_name));
	} else {
		index   = fp->f_offset - 2;

		print("[mfs] readdir index: %llu\n", index);

		if (index >= inode->dir_children_count) {
			mutex_unlock(&mfs_lock);
			return ENOENT;
		}

		block  += MFS_RECORD_BLOCK(sb->block_size, index);
		offset  = index % (sb->block_size / sizeof(struct mfs_dir_record));

		print("[mfs] readdir block: %llu\n", block);
		print("[mfs] readdir offset: %llu\n", offset);

		error = bread(device, block, &bh);
		if (error) {
        	kprintf("[mfs] Error reading block [%llu]\n", block);
        	mutex_unlock(&mfs_lock);
        	return ENOENT;
    	}

    	record = (struct mfs_dir_record*)bh->b_data;
    	record += offset;

    	if (S_ISDIR(inode->mode))
    		dir->d_type = DT_DIR;
    	else
    		dir->d_type = DT_REG;

    	// Set the name
    	strlcpy((char *)&dir->d_name, record->filename, sizeof(dir->d_name));

    	dir->d_ino = record->inode_no;
    	dir->d_fileno = fp->f_offset;

    	brelse(bh);

	}

	fp->f_offset++;

	mutex_unlock(&mfs_lock);

	return 0;
}

static int mfs_lookup(struct vnode *vnode, char *name, struct vnode **vpp) {
	print("[mfs] mfs_lookup called: %s\n", name);

	mutex_lock(&mfs_lock);

	struct mfs_inode       *inode   = (struct mfs_inode*)vnode->v_data;
	struct mfs_super_block *sb      = (struct mfs_super_block*)vnode->v_mount->m_data;
	struct device          *device  = vnode->v_mount->m_dev;
	struct mfs_inode       *r_inode = NULL;
	struct mfs_dir_record  *records = NULL;
	struct buf             *bh      = NULL;
	struct vnode           *vp      = NULL;

	int      error  = -1;
	uint64_t i      =  0;
	uint64_t block  =  inode->data_block_number;
	uint64_t c      =  0;

	if (*name == '\0')
		return ENOENT;

	while (r_inode == NULL) {
		error = bread(device, block, &bh);
		if (error) {
	    	kprintf("[mfs] Error reading block [%llu]\n", block);
	    	mutex_unlock(&mfs_lock);
	    	return ENOENT;
	    }

	    records = (struct mfs_dir_record *)bh->b_data;
	    for (i = 0; i < MFS_RECORDS_PER_BLOCK(sb->block_size); i++) {
	    	if (strcmp(name, records[i].filename) == 0) {
	    		// Found!
	    		print("[mfs] found the directory entry!\n");
	    		r_inode = mfs_get_inode(sb, device, records[i].inode_no);
	    		break;
	    	}
	    	c++;
	    	if (c >= inode->dir_children_count) {
	    		break;
	    	}
	    }
	    brelse(bh);
	    // If we looked at every entry and still havnt found it
	    if (c >= inode->dir_children_count && r_inode == NULL) {
	    	mutex_unlock(&mfs_lock);
	    	return ENOENT;
	    } else {
	    	// Move on to the next block!
	    	block++;
	    }
	}

	print("[mfs] mfs_lookup using inode: %llu\n", r_inode->inode_no);

	if (vget(vnode->v_mount, r_inode->inode_no, &vp)) {
		print("[mfs] found vp in cache!\n");
		// Found in cache?
		*vpp = vp;
		mutex_unlock(&mfs_lock);
		return 0;
	}

	print("[mfs] got vp: %p\n", vp);

	if (!vp) {
		mutex_unlock(&mfs_lock);
		return ENOMEM;
	}

	mfs_set_vnode(vp, r_inode);

	*vpp = vp;

	mutex_unlock(&mfs_lock);

	return 0;
}

static int mfs_getattr(struct vnode *vnode, struct vattr *attr) {
	struct mfs_inode *inode = (struct mfs_inode*)vnode->v_data;

	// Doesn't seem to work, I think permissions are hard coded to 777
	attr->va_mode = 00555;
	
	if (S_ISDIR(inode->mode)) {
		attr->va_type = VDIR;
	} else {
		attr->va_type = VREG;
	}

	attr->va_nodeid = vnode->v_ino;
	attr->va_size = vnode->v_size;

	return 0;
}


#define mfs_seek		((vnop_seek_t)vop_nullop)
#define mfs_ioctl		((vnop_ioctl_t)vop_nullop)
#define mfs_inactive	((vnop_inactive_t)vop_nullop)
#define mfs_truncate	((vnop_truncate_t)vop_nullop)
#define mfs_link     	((vnop_link_t)vop_nullop)
#define mfs_arc			((vnop_cache_t) nullptr)
#define mfs_fallocate	((vnop_fallocate_t)vop_nullop)
#define mfs_fsync		((vnop_fsync_t)vop_nullop)
#define mfs_readlink	((vnop_readlink_t)vop_nullop)
#define mfs_symlink		((vnop_symlink_t)vop_nullop)

struct vnops mfs_vnops = {
	mfs_open,			/* open */
	mfs_close,			/* close */
	mfs_read,			/* read */
	NULL,	     		/* write - not impelemented */
	mfs_seek,			/* seek */
	mfs_ioctl,			/* ioctl */
	mfs_fsync,			/* fsync */
	mfs_readdir,		/* readdir */
	mfs_lookup,			/* lookup */
	NULL,				/* create - not impelemented */
	NULL,				/* remove - not impelemented */
	NULL,				/* rename - not impelemented */
	NULL,				/* mkdir - not impelemented */
	NULL,				/* rmdir - not impelemented */
	mfs_getattr,		/* getattr */
	NULL,				/* setattr - not impelemented */
	mfs_inactive, 		/* inactive */
	mfs_truncate,		/* truncate */
	mfs_link,			/* link */
	mfs_arc,			/* arc */
	mfs_fallocate,		/* fallocate */
	mfs_readlink,		/* read link */
	mfs_symlink		/* symbolic link */
};
