/*
 * Copyright (C) 2024 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

//Most of the code in this file is modeled after corresponding vnops
//implementation in ZFS, RoFS and RamFS filesystems but also loosely
//after the code in src/ext4.c from lwext4 library. The internal functions
//ext_internal_read() and ext_internal_write() on other hand are almost verbatim
//taken from ext4_read() and ext4_write() from the same file and slighly
//adjusted to C++.
//
//In effect, this vnops implementation bypasses the ext4.c layer of the lwext4
//library and interacts with lower-layer functions like ext4_block_*(), ext4_dir_*(),
//ext4_fs_*() and ext4_inode_*() in a similar way the original ext4.c does.
//
//WARNING: This implementation is functional enough for all tests in the test.sh
//to pass. But it is NOT thread-safe yet. To make it so, we will need to synchronize
//access to block cache, i-node and block allocation routines as well as updating
//super block.
//
//Also, it does not implement journal (we can integrate it later and make it optional)
//nor xattr which is not even supported by OSv VFS layer.

extern "C" {
#define USE_C_INTERFACE 1
#include <osv/device.h>
#include <osv/prex.h>
#include <osv/vnode.h>
#include <osv/mount.h>
#include <osv/debug.h>
#include <osv/file.h>
#include <osv/vnode_attr.h>

void* alloc_contiguous_aligned(size_t size, size_t align);
void free_contiguous_aligned(void* p);
}

#include <ext4_errno.h>
#include <ext4_dir.h>
#include <ext4_inode.h>
#include <ext4_fs.h>
#include <ext4_dir_idx.h>
#include <ext4_trans.h>

#include <cstdlib>
#include <time.h>
#include <cstddef>

#include <algorithm>

//#define CONF_debug_ext 1
#if CONF_debug_ext
#define ext_debug(format,...) kprintf("[ext4] " format, ##__VA_ARGS__)
#else
#define ext_debug(...)
#endif

//Simple RAII struct to automate release of i-node reference
//when it goes out of scope.
struct auto_inode_ref {
    struct ext4_inode_ref _ref;
    int _r;

    auto_inode_ref(struct ext4_fs *fs, uint32_t inode_no) {
        _r = ext4_fs_get_inode_ref(fs, inode_no, &_ref);
    }
    ~auto_inode_ref() {
        if (_r == EOK) {
            ext4_fs_put_inode_ref(&_ref);
        }
    }
};

//Simple RAII struct to set boundaries around ext4 function calls
//with block cache write back enabled. Effectively, when the instance
//of this struct goes out of scope, the writes are flushed to disk
//and write back disabled.
struct auto_write_back {
    struct ext4_fs *_fs;

    auto_write_back(struct ext4_fs *fs) {
        _fs = fs;
        ext4_block_cache_write_back(_fs->bdev, 1);
    }

    ~auto_write_back() {
        ext4_block_cache_write_back(_fs->bdev, 0);
    }
};

typedef	struct vnode vnode_t;
typedef	struct file file_t;
typedef struct uio uio_t;
typedef	off_t offset_t;
typedef	struct vattr vattr_t;

//TODO:
//Ops:
// - ext_ioctl
// - ext_fsync
//
// Later:
// - ext_arc
// - ext_fallocate - Linux specific

static int
ext_open(struct file *fp)
{
    ext_debug("Opening file\n");
    return (EOK);
}

static int
ext_close(vnode_t *vp, file_t *fp)
{
    ext_debug("Closing file\n");
    return (EOK);
}

static int
ext_internal_read(struct ext4_fs *fs, struct ext4_inode_ref *ref, uint64_t offset, void *buf, size_t size, size_t *rcnt)
{
    ext4_fsblk_t fblock;
    ext4_fsblk_t fblock_start;

    uint8_t *u8_buf = (uint8_t *)buf;
    int r;

    if (!size)
        return EOK;

    struct ext4_sblock *const sb = &fs->sb;

    if (rcnt)
        *rcnt = 0;

    /*Sync file size*/
    uint64_t fsize = ext4_inode_get_size(sb, ref->inode);

    uint32_t block_size = ext4_sb_get_block_size(sb);
    size = ((uint64_t)size > (fsize - offset))
        ? ((size_t)(fsize - offset)) : size;

    uint32_t iblock_idx = (uint32_t)((offset) / block_size);
    uint32_t iblock_last = (uint32_t)((offset + size) / block_size);
    uint32_t unalg = (offset) % block_size;

    uint32_t fblock_count = 0;
    if (unalg) {
        size_t len =  size;
        if (size > (block_size - unalg))
            len = block_size - unalg;

        r = ext4_fs_get_inode_dblk_idx(ref, iblock_idx, &fblock, true);
        if (r != EOK)
            goto Finish;

        /* Do we get an unwritten range? */
        if (fblock != 0) {
            uint64_t off = fblock * block_size + unalg;
            r = ext4_block_readbytes(fs->bdev, off, u8_buf, len);
            if (r != EOK)
                goto Finish;

        } else {
            /* Yes, we do. */
            memset(u8_buf, 0, len);
        }

        u8_buf += len;
        size -= len;
        offset += len;

        if (rcnt)
            *rcnt += len;

        iblock_idx++;
    }

    fblock_start = 0;
    while (size >= block_size) {
        while (iblock_idx < iblock_last) {
            r = ext4_fs_get_inode_dblk_idx(ref, iblock_idx,
                               &fblock, true);
            if (r != EOK)
                goto Finish;

            iblock_idx++;

            if (!fblock_start)
                fblock_start = fblock;

            if ((fblock_start + fblock_count) != fblock)
                break;

            fblock_count++;
        }

        ext_debug("ext4_blocks_get_direct: block_start:%ld, block_count:%d\n", fblock_start, fblock_count);
        r = ext4_blocks_get_direct(fs->bdev, u8_buf, fblock_start,
                       fblock_count);
        if (r != EOK)
            goto Finish;

        size -= block_size * fblock_count;
        u8_buf += block_size * fblock_count;
        offset += block_size * fblock_count;

        if (rcnt)
            *rcnt += block_size * fblock_count;

        fblock_start = fblock;
        fblock_count = 1;
    }

    if (size) {
        r = ext4_fs_get_inode_dblk_idx(ref, iblock_idx, &fblock, true);
        if (r != EOK)
            goto Finish;

        uint64_t off = fblock * block_size;
        ext_debug("ext4_block_readbytes: off:%ld, size:%ld\n", off, size);
        r = ext4_block_readbytes(fs->bdev, off, u8_buf, size);
        if (r != EOK)
            goto Finish;

        offset += size;

        if (rcnt)
            *rcnt += size;
    }

Finish:
    return r;
}

static int
ext_read(vnode_t *vp, struct file *fp, uio_t *uio, int ioflag)
{
    ext_debug("Reading %ld bytes at offset:%ld from file i-node:%ld\n", uio->uio_resid, uio->uio_offset, vp->v_ino);

    /* Cant read directories */
    if (vp->v_type == VDIR)
        return EISDIR;

    /* Cant read anything but reg */
    if (vp->v_type != VREG)
        return EINVAL;

    /* Cant start reading before the first byte */
    if (uio->uio_offset < 0)
        return EINVAL;

    /* Need to read more than 1 byte */
    if (uio->uio_resid == 0)
        return 0;

    struct ext4_fs *fs = (struct ext4_fs *)vp->v_mount->m_data;
    auto_inode_ref inode_ref(fs, vp->v_ino);
    if (inode_ref._r != EOK) {
        return inode_ref._r;
    }

    // Total read amount is what they requested, or what is left
    uint64_t fsize = ext4_inode_get_size(&fs->sb, inode_ref._ref.inode);
    uint64_t read_amt = std::min(fsize - uio->uio_offset, (uint64_t)uio->uio_resid);
    void *buf = alloc_contiguous_aligned(read_amt, alignof(std::max_align_t));

    size_t read_count = 0;
    int ret = ext_internal_read(fs, &inode_ref._ref, uio->uio_offset, buf, read_amt, &read_count);
    if (ret) {
        kprintf("[ext_read] Error reading data\n");
        free(buf);
        return ret;
    }

    ret = uiomove(buf, read_count, uio);
    free_contiguous_aligned(buf);

    return ret;
}

static int
ext_internal_write(struct ext4_fs *fs, struct ext4_inode_ref *ref, uint64_t offset, void *buf, size_t size, size_t *wcnt)
{
    ext_debug("[ext4_internal_write] Writing %ld bytes at offset:%ld\n", size, offset);
    ext4_fsblk_t fblock;
    ext4_fsblk_t fblock_start = 0;

    uint8_t *u8_buf = (uint8_t *)buf;
    int r, rr = EOK;

    if (!size)
        return EOK;

    struct ext4_sblock *const sb = &fs->sb;

    if (wcnt)
        *wcnt = 0;

    /*Sync file size*/
    uint64_t fsize = ext4_inode_get_size(sb, ref->inode);
    uint32_t block_size = ext4_sb_get_block_size(sb);

    uint32_t iblock_last = (uint32_t)((offset + size) / block_size);
    uint32_t iblk_idx = (uint32_t)(offset / block_size);
    uint32_t ifile_blocks = (uint32_t)((fsize + block_size - 1) / block_size);

    uint32_t unalg = (offset) % block_size;

    uint32_t fblock_count = 0;

    if (unalg) {
        size_t len = size;
        uint64_t off;
        if (size > (block_size - unalg))
            len = block_size - unalg;

        if (iblk_idx < ifile_blocks) {
            r = ext4_fs_init_inode_dblk_idx(ref, iblk_idx, &fblock);
        }
        else {
            r = ext4_fs_append_inode_dblk(ref, &fblock, &iblk_idx);
            ext_debug("[ext_internal_write] Appended block=%d, phys:%ld\n", iblk_idx, fblock);
            ifile_blocks++;
        }
        if (r != EOK)
            goto Finish;

        off = fblock * block_size + unalg;
        r = ext4_block_writebytes(fs->bdev, off, u8_buf, len);
        ext_debug("[ext_internal_write] Wrote unaligned %ld bytes at %ld\n", len, off);
        if (r != EOK)
            goto Finish;

        u8_buf += len;
        size -= len;
        offset += len;

        if (wcnt)
            *wcnt += len;

        iblk_idx++;
    }

    /*Start write back cache mode.*/
    r = ext4_block_cache_write_back(fs->bdev, 1);
    if (r != EOK)
        goto Finish;

    //Sometimes file size is less than caller what to start writing at
    //For example, it is valid to lseek() with SEEK_END with offset to position
    //file for writing beyond its size.
    //On Linux, the ext4 supports it as a sparse file but our lwext4-based
    //implementation does not support sparse files really. So in such case,
    //we simply append as many missing blocks as needed to close the gap
    while (ifile_blocks < iblk_idx) {
        uint32_t iblk_idx2;
        auto res = ext4_fs_append_inode_dblk(ref, nullptr, &iblk_idx2);
        if (res != EOK) {
            offset = ifile_blocks * block_size;
            goto out_fsize;
        }
        ext_debug("[ext_internal_write] Appended (2) block=%d\n", iblk_idx2);
        ifile_blocks++;
    }

    while (size >= block_size) {

        while (iblk_idx < iblock_last) {
            if (iblk_idx < ifile_blocks) {
                r = ext4_fs_init_inode_dblk_idx(ref, iblk_idx,
                                &fblock);
                if (r != EOK)
                    goto Finish;
            } else {
                rr = ext4_fs_append_inode_dblk(ref, &fblock,
                                   &iblk_idx);
                ext_debug("[ext_internal_write] Appended (3) block=%d, phys:%ld\n", iblk_idx, fblock);
                if (rr != EOK) {
                    /* Unable to append more blocks. But
                     * some block might be allocated already
                     * */
                    break;
                }
            }

            iblk_idx++;

            if (!fblock_start) {
                fblock_start = fblock;
            }

            if ((fblock_start + fblock_count) != fblock)
                break;

            fblock_count++;
        }

        r = ext4_blocks_set_direct(fs->bdev, u8_buf, fblock_start,
                       fblock_count);
        ext_debug("[ext_internal_write] Wrote direct %d blocks at block %ld\n", fblock_count, fblock_start);
        if (r != EOK)
            break;

        size -= block_size * fblock_count;
        u8_buf += block_size * fblock_count;
        offset += block_size * fblock_count;

        if (wcnt)
            *wcnt += block_size * fblock_count;

        fblock_start = fblock;
        fblock_count = 1;

        if (rr != EOK) {
            /*ext4_fs_append_inode_block has failed and no
             * more blocks might be written. But node size
             * should be updated.*/
            r = rr;
            goto out_fsize;
        }
    }

    /*Stop write back cache mode*/
    ext4_block_cache_write_back(fs->bdev, 0);

    if (r != EOK)
        goto Finish;

    if (size) {
        uint64_t off;
        if (iblk_idx < ifile_blocks) {
            r = ext4_fs_init_inode_dblk_idx(ref, iblk_idx, &fblock);
            if (r != EOK)
                goto Finish;
        } else {
            r = ext4_fs_append_inode_dblk(ref, &fblock, &iblk_idx);
            ext_debug("[ext_internal_write] Appended (4) block=%d, phys:%ld\n", iblk_idx, fblock);
            if (r != EOK)
                /*Node size sholud be updated.*/
                goto out_fsize;
        }

        off = fblock * block_size;
        r = ext4_block_writebytes(fs->bdev, off, u8_buf, size);
        ext_debug("[ext_internal_write] Wrote remaining %ld bytes at %ld\n", size, off);
        if (r != EOK)
            goto Finish;

        offset += size;

        if (wcnt)
            *wcnt += size;
    }

out_fsize:
    if (offset > fsize) {
        ext4_inode_set_size(ref->inode, offset);
        ref->dirty = true;
    }

Finish:
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    ext4_inode_set_change_inode_time(ref->inode, now.tv_sec);
    ext4_inode_set_modif_time(ref->inode, now.tv_sec);
    ref->dirty = true;

    return r;
}

static int
ext_write(vnode_t *vp, uio_t *uio, int ioflag)
{
    ext_debug("Writing %ld bytes at offset:%ld to file i-node:%ld\n", uio->uio_resid, uio->uio_offset, vp->v_ino);

    /* Cant write directories */
    if (vp->v_type == VDIR)
        return EISDIR;

    /* Cant write anything but reg */
    if (vp->v_type != VREG)
        return EINVAL;

    /* Cant start writing before the first byte */
    if (uio->uio_offset < 0)
        return EINVAL;

    /* Need to write more than 1 byte */
    if (uio->uio_resid == 0)
        return 0;

    struct ext4_fs *fs = (struct ext4_fs *)vp->v_mount->m_data;
    auto_inode_ref inode_ref(fs, vp->v_ino);
    if (inode_ref._r != EOK) {
        return inode_ref._r;
    }

    uio_t uio_copy = *uio;
    if (ioflag & IO_APPEND) {
        uio_copy.uio_offset = ext4_inode_get_size(&fs->sb, inode_ref._ref.inode);
    }

    void *buf = alloc_contiguous_aligned(uio->uio_resid, alignof(std::max_align_t));
    int ret = uiomove(buf, uio->uio_resid, &uio_copy);
    if (ret) {
        kprintf("[ext_write] Error copying data\n");
        free(buf);
        return ret;
    }

    size_t write_count = 0;
    ret = ext_internal_write(fs, &inode_ref._ref, uio->uio_offset, buf, uio->uio_resid, &write_count);

    uio->uio_resid -= write_count;
    free_contiguous_aligned(buf);

    return ret;
}

static int
ext_ioctl(vnode_t *vp, file_t *fp, u_long com, void *data)
{
    ext_debug("ioctl\n");
    return (EINVAL);
}

static int
ext_fsync(vnode_t *vp, file_t *fp)
{
    ext_debug("fsync\n");
    return (EINVAL);
}

static int
ext_readdir(struct vnode *dvp, struct file *fp, struct dirent *dir)
{
#define EXT4_DIR_ENTRY_OFFSET_TERM (uint64_t)(-1)
    struct ext4_fs *fs = (struct ext4_fs *)dvp->v_mount->m_data;
    struct ext4_inode_ref inode_ref;

    if (file_offset(fp) == 1) {//EXT4_DIR_ENTRY_OFFSET_TERM) {
        return ENOENT;
    }

    int r = ext4_fs_get_inode_ref(fs, dvp->v_ino, &inode_ref);
    if (r != EOK) {
        return r;
    }

    /* Check if node is directory */
    if (!ext4_inode_is_type(&fs->sb, inode_ref.inode, EXT4_INODE_MODE_DIRECTORY)) {
        ext4_fs_put_inode_ref(&inode_ref);
        return ENOTDIR;
    }

    ext_debug("Reading directory with i-node:%ld at offset:%ld\n", dvp->v_ino, file_offset(fp));
    struct ext4_dir_iter it;
    int rc = ext4_dir_iterator_init(&it, &inode_ref, file_offset(fp));
    if (rc != EOK) {
        kprintf("[ext4] Reading directory with i-node:%ld at offset:%ld -> FAILED to init iterator\n", dvp->v_ino, file_offset(fp));
        ext4_fs_put_inode_ref(&inode_ref);
        return rc;
    }

    /* Test for non-empty directory entry */
    if (it.curr != NULL) {
        if (ext4_dir_en_get_inode(it.curr) != 0) {
            memset(dir->d_name, 0, sizeof(dir->d_name));
            uint16_t name_length = ext4_dir_en_get_name_len(&fs->sb, it.curr);
            memcpy(dir->d_name, it.curr->name, name_length);
            ext_debug("Reading directory with i-node:%ld at offset:%ld => entry name:%s\n", dvp->v_ino, file_offset(fp), dir->d_name);

            dir->d_ino = ext4_dir_en_get_inode(it.curr);

            uint8_t i_type = ext4_dir_en_get_inode_type(&fs->sb, it.curr);
            if (i_type == EXT4_DE_DIR) {
                dir->d_type = DT_DIR;
            } else if (i_type == EXT4_DE_REG_FILE) {
                dir->d_type = DT_REG;
            } else if (i_type == EXT4_DE_SYMLINK) {
                dir->d_type = DT_LNK;
            }

            ext4_dir_iterator_next(&it);

            off_t f_offset = file_offset(fp);
            dir->d_fileno = f_offset;
            dir->d_off = f_offset + 1;
            file_setoffset(fp, it.curr ? it.curr_off : EXT4_DIR_ENTRY_OFFSET_TERM);
        } else {
            ext_debug("Reading directory with i-node:%ld at offset:%ld -> cos ni tak\n", dvp->v_ino, file_offset(fp));
        }
    } else {
        ext4_dir_iterator_fini(&it);
        ext4_fs_put_inode_ref(&inode_ref);
        ext_debug("Reading directory with i-node:%ld at offset:%ld -> ENOENT\n", dvp->v_ino, file_offset(fp));
        return ENOENT;
    }

    rc = ext4_dir_iterator_fini(&it);
    ext4_fs_put_inode_ref(&inode_ref);
    if (rc != EOK)
        return rc;

    return EOK;
}

static int
ext_lookup(struct vnode *dvp, char *nm, struct vnode **vpp)
{
    ext_debug("Looking up %s in directory with i-node:%ld\n", nm, dvp->v_ino);
    struct ext4_fs *fs = (struct ext4_fs *)dvp->v_mount->m_data;

    auto_inode_ref inode_ref(fs, dvp->v_ino);
    if (inode_ref._r != EOK) {
        return inode_ref._r;
    }

    /* Check if node is directory */
    if (!ext4_inode_is_type(&fs->sb, inode_ref._ref.inode, EXT4_INODE_MODE_DIRECTORY)) {
        return ENOTDIR;
    }

    struct ext4_dir_search_result result;
    int r = ext4_dir_find_entry(&result, &inode_ref._ref, nm, strlen(nm));
    if (r == EOK) {
        uint32_t inode_no = ext4_dir_en_get_inode(result.dentry);
        vget(dvp->v_mount, inode_no, vpp);

        auto_inode_ref inode_ref2(fs, inode_no);
        if (inode_ref2._r != EOK) {
            return inode_ref2._r;
        }

        uint32_t i_type = ext4_inode_type(&fs->sb, inode_ref2._ref.inode);
        if (i_type == EXT4_INODE_MODE_DIRECTORY) {
            (*vpp)->v_type = VDIR;
        } else if (i_type == EXT4_INODE_MODE_FILE) {
            (*vpp)->v_type = VREG;
        } else if (i_type == EXT4_INODE_MODE_SOFTLINK) {
            (*vpp)->v_type = VLNK;
        }

        (*vpp)->v_mode = ext4_inode_get_mode(&fs->sb, inode_ref2._ref.inode);

        ext_debug("Looked up %s %s in directory with i-node:%ld as i-node:%d\n",
            (*vpp)->v_type == VDIR ? "DIR" : ((*vpp)->v_type == VREG ? "FILE" : "SYMLINK"),
            nm, dvp->v_ino, inode_no);
    } else {
        r = ENOENT;
    }

    ext4_dir_destroy_result(&inode_ref._ref, &result);

    return r;
}

static int
ext_dir_initialize(ext4_inode_ref *parent, ext4_inode_ref *child, bool dir_index_on)
{
    int r;
#if CONFIG_DIR_INDEX_ENABLE
    /* Initialize directory index if supported */
    if (dir_index_on) {
        ext_debug("DIR_INDEX on initializing directory with inode no:%d\n", child->index);
        r = ext4_dir_dx_init(child, parent);
        if (r != EOK)
            return r;

        ext4_inode_set_flag(child->inode, EXT4_INODE_FLAG_INDEX);
    } else
#endif
    {
        r = ext4_dir_add_entry(child, ".", strlen("."), child);
        if (r != EOK) {
            return r;
        }

        r = ext4_dir_add_entry(child, "..", strlen(".."), parent);
        if (r != EOK) {
            ext4_dir_remove_entry(child, ".", strlen("."));
            return r;
        }
    }

    /*New empty directory. Two links (. and ..) */
    ext4_inode_set_links_cnt(child->inode, 2);
    ext4_fs_inode_links_count_inc(parent);
    parent->dirty = true;
    child->dirty = true;

    return r;
}

static int
ext_dir_link(struct vnode *dvp, char *name, int file_type, uint32_t *inode_no, uint32_t *inode_no_created)
{
    struct ext4_fs *fs = (struct ext4_fs *)dvp->v_mount->m_data;
    auto_write_back wb(fs);
    auto_inode_ref inode_ref(fs, dvp->v_ino);
    if (inode_ref._r != EOK) {
        return inode_ref._r;
    }

    /* Check if node is directory */
    if (!ext4_inode_is_type(&fs->sb, inode_ref._ref.inode, EXT4_INODE_MODE_DIRECTORY)) {
        return ENOTDIR;
    }

    struct ext4_dir_search_result result;
    int r = ext4_dir_find_entry(&result, &inode_ref._ref, name, strlen(name));
    ext4_dir_destroy_result(&inode_ref._ref, &result);
    if (r == EOK) {
        ext_debug("%s already exists under i-node %li\n", name, dvp->v_ino);
        return EEXIST;
    }

    struct ext4_inode_ref child_ref;
    if (inode_no) {
        r = ext4_fs_get_inode_ref(fs, *inode_no, &child_ref);
    } else {
        r = ext4_fs_alloc_inode(fs, &child_ref, file_type);
    }
    if (r != EOK) {
        return r;
    }

    if (!inode_no ) {
        ext4_fs_inode_blocks_init(fs, &child_ref);
    }

    r = ext4_dir_add_entry(&inode_ref._ref, name, strlen(name), &child_ref);
    if (r == EOK) {
        bool is_dir = ext4_inode_is_type(&fs->sb, child_ref.inode, EXT4_INODE_MODE_DIRECTORY);
        if (is_dir && inode_no) {
            r = EPERM; //Cannot create hard links for directories
        } else if (is_dir) {
#if CONFIG_DIR_INDEX_ENABLE
            bool dir_index_on = ext4_sb_feature_com(&fs->sb, EXT4_FCOM_DIR_INDEX);
#else
            bool dir_index_on = false;
#endif
            ext_debug("initializing directory %s with i-node:%d\n", name, child_ref.index);
            r = ext_dir_initialize(&inode_ref._ref, &child_ref, dir_index_on);
            if (r != EOK) {
                ext4_dir_remove_entry(&inode_ref._ref, name, strlen(name));
            }
        } else {
            ext4_fs_inode_links_count_inc(&child_ref);
        }
    }

    if (r == EOK) {
        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        ext4_inode_set_change_inode_time(child_ref.inode, now.tv_sec);
        if (!inode_no) {
            ext4_inode_set_access_time(child_ref.inode, now.tv_sec);
            ext4_inode_set_modif_time(child_ref.inode, now.tv_sec);
        }

        ext4_inode_set_change_inode_time(inode_ref._ref.inode, now.tv_sec);
        ext4_inode_set_modif_time(inode_ref._ref.inode, now.tv_sec);

        inode_ref._ref.dirty = true;
        child_ref.dirty = true;
        if (inode_no_created) {
            *inode_no_created = child_ref.index;
        }
        ext_debug("created %s under i-node %li\n", name, dvp->v_ino);
    } else {
        if (!inode_no) {
            ext4_fs_free_inode(&child_ref);
        }
        //We do not want to write new inode. But block has to be released.
        kprintf("[ext4] failed to create %s under i-node %li due to error:%d!\n", name, dvp->v_ino, r);
        child_ref.dirty = false;
    }

    ext4_fs_put_inode_ref(&child_ref);

    return r;
}

static int
ext_create(struct vnode *dvp, char *name, mode_t mode)
{
    ext_debug("create %s under i-node %li\n", name, dvp->v_ino);

    uint32_t len = strlen(name);
    if (len > NAME_MAX || len > EXT4_DIRECTORY_FILENAME_LEN) {
        return ENAMETOOLONG;
    }

    if (!S_ISREG(mode))
        return EINVAL;

    return ext_dir_link(dvp, name, EXT4_DE_REG_FILE, nullptr, nullptr);
}

static int
ext_trunc_inode(struct ext4_fs *fs, uint32_t index, uint64_t new_size)
{
    struct ext4_inode_ref inode_ref;
    int r = ext4_fs_get_inode_ref(fs, index, &inode_ref);
    if (r != EOK)
        return r;

    uint64_t inode_size = ext4_inode_get_size(&fs->sb, inode_ref.inode);
    ext4_fs_put_inode_ref(&inode_ref);
/*
    bool has_trans = mp->fs.jbd_journal && mp->fs.curr_trans;
    if (has_trans)
        ext4_trans_stop(mp);*/

    while (inode_size > new_size + CONFIG_MAX_TRUNCATE_SIZE) {

        inode_size -= CONFIG_MAX_TRUNCATE_SIZE;

        //ext4_trans_start(mp);
        r = ext4_fs_get_inode_ref(fs, index, &inode_ref);
        if (r != EOK) {
            //ext4_trans_abort(mp);
            break;
        }
        r = ext4_fs_truncate_inode(&inode_ref, inode_size);
        if (r != EOK)
            ext4_fs_put_inode_ref(&inode_ref);
        else
            r = ext4_fs_put_inode_ref(&inode_ref);

        if (r != EOK) {
            //ext4_trans_abort(mp);
            goto Finish;
        }/* else
            ext4_trans_stop(mp);*/
    }

    if (inode_size > new_size) {
        inode_size = new_size;

        //ext4_trans_start(mp);
        r = ext4_fs_get_inode_ref(fs, index, &inode_ref);
        if (r != EOK) {
            //ext4_trans_abort(mp);
            goto Finish;
        }
        r = ext4_fs_truncate_inode(&inode_ref, inode_size);
        if (r != EOK)
            ext4_fs_put_inode_ref(&inode_ref);
        else
            r = ext4_fs_put_inode_ref(&inode_ref);
/*
        if (r != EOK)
            ext4_trans_abort(mp);
        else
            ext4_trans_stop(mp);*/

    }

Finish:

    /*if (has_trans)
        ext4_trans_start(mp);*/

    return r;
}

static int
ext_dir_trunc(struct ext4_fs *fs, struct ext4_inode_ref *parent, struct ext4_inode_ref *dir)
{
    int r = EOK;
    uint32_t block_size = ext4_sb_get_block_size(&fs->sb);

#if CONFIG_DIR_INDEX_ENABLE
    /* Initialize directory index if supported */
    if (ext4_sb_feature_com(&fs->sb, EXT4_FCOM_DIR_INDEX)) {
        r = ext4_dir_dx_init(dir, parent);
        if (r != EOK)
            return r;

        r = ext_trunc_inode(fs, dir->index,
                     EXT4_DIR_DX_INIT_BCNT * block_size);
        if (r != EOK)
            return r;
    } else
#endif
    {
        r = ext_trunc_inode(fs, dir->index, block_size);
        if (r != EOK)
            return r;
    }

    return ext4_fs_truncate_inode(dir, 0);
}

static int
ext_dir_remove_entry(struct vnode *dvp, struct vnode *vp, char *name)
{
    struct ext4_fs *fs = (struct ext4_fs *)dvp->v_mount->m_data;
    auto_write_back wb(fs);
    auto_inode_ref parent(fs, dvp->v_ino);
    if (parent._r != EOK) {
        return parent._r;
    }

    auto_inode_ref child(fs, vp->v_ino);
    if (child._r != EOK) {
        return child._r;
    }

    int r = EOK;
    uint32_t inode_type = ext4_inode_type(&fs->sb, child._ref.inode);
    if (inode_type != EXT4_INODE_MODE_DIRECTORY) {
        if (ext4_inode_get_links_cnt(child._ref.inode) == 1) {
            r = ext_trunc_inode(fs, child._ref.index, 0);
            if (r != EOK) {
                return r;
            }
        }
    } else {
        r = ext_dir_trunc(fs, &parent._ref, &child._ref);
        if (r != EOK) {
            return r;
        }
    }

    /* Remove entry from parent directory */
    r = ext4_dir_remove_entry(&parent._ref, name, strlen(name));
    if (r != EOK) {
        return r;
    }

    if (inode_type != EXT4_INODE_MODE_DIRECTORY) {
        int links_cnt = ext4_inode_get_links_cnt(child._ref.inode);
        if (links_cnt) {
            ext4_fs_inode_links_count_dec(&child._ref);
            child._ref.dirty = true;

            if (links_cnt == 1) {//Zero now
                ext4_fs_free_inode(&child._ref);
            }
        }
    } else {
        ext4_fs_free_inode(&child._ref);
    }

    if (r == EOK) {
        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        ext4_inode_set_change_inode_time(parent._ref.inode, now.tv_sec);
        ext4_inode_set_modif_time(parent._ref.inode, now.tv_sec);

        parent._ref.dirty = true;
    }

    return r;
}

static int
ext_remove(struct vnode *dvp, struct vnode *vp, char *name)
{
    ext_debug("remove\n");
    return ext_dir_remove_entry(dvp, vp, name);
}

static int
ext_rename(struct vnode *sdvp, struct vnode *svp, char *snm,
           struct vnode *tdvp, struct vnode *tvp, char *tnm)
{
    ext_debug("rename\n");
    struct ext4_fs *fs = (struct ext4_fs *)sdvp->v_mount->m_data;
    auto_write_back wb(fs);

    int r = EOK;
    if (tvp) {
        // Remove destination file, first ... if exists
        ext_debug("rename removing %s from the target directory\n", tnm);
        auto_inode_ref target_dir(fs, tdvp->v_ino);
        if (target_dir._r != EOK) {
            return target_dir._r;
        }
        /* Remove entry from target directory */
        r = ext4_dir_remove_entry(&target_dir._ref, tnm, strlen(tnm));
        if (r != EOK) {
            return r;
        }
    }

    auto_inode_ref src_dir(fs, sdvp->v_ino);
    if (src_dir._r != EOK) {
        return src_dir._r;
    }

    auto_inode_ref src_entry(fs, svp->v_ino);
    if (src_entry._r != EOK) {
        return src_entry._r;
    }

    /* Same directory ? */
    if (sdvp == tdvp) {
        // Add new entry to the same directory
        r = ext4_dir_add_entry(&src_dir._ref, tnm, strlen(tnm), &src_entry._ref);
        if (r != EOK) {
            return r;
        }
    } else {
        // Add new entry to the destination directory
        auto_inode_ref dest_dir(fs, tdvp->v_ino);
        if (dest_dir._r != EOK) {
            return dest_dir._r;
        }

        r = ext4_dir_add_entry(&dest_dir._ref, tnm, strlen(tnm), &src_entry._ref);
        if (r != EOK) {
            return r;
        }
    }

    // If directory need to reposition '..' to different parent - target directory
    if (ext4_inode_is_type(&fs->sb, src_entry._ref.inode, EXT4_INODE_MODE_DIRECTORY)) {
        auto_inode_ref dest_dir(fs, tdvp->v_ino);
        if (dest_dir._r != EOK) {
            return dest_dir._r;
        }

        bool idx;
        idx = ext4_inode_has_flag(src_entry._ref.inode, EXT4_INODE_FLAG_INDEX);
        struct ext4_dir_search_result res;
        if (!idx) {
            r = ext4_dir_find_entry(&res, &src_entry._ref, "..", strlen(".."));
            if (r != EOK)
                return EIO;

            ext4_dir_en_set_inode(res.dentry, dest_dir._ref.index);
            ext4_trans_set_block_dirty(res.block.buf);
            r = ext4_dir_destroy_result(&src_entry._ref, &res);
            if (r != EOK)
                return r;

        } else {
#if CONFIG_DIR_INDEX_ENABLE
            r = ext4_dir_dx_reset_parent_inode(&src_entry._ref, dest_dir._ref.index);
            if (r != EOK)
                return r;

#endif
        }

        ext4_fs_inode_links_count_inc(&dest_dir._ref);
        dest_dir._ref.dirty = true;
    }

    /* Remove old entry from the source directory */
    r = ext4_dir_remove_entry(&src_dir._ref, snm, strlen(snm));
    if (r != EOK) {
        return r;
    }

    return r;
}

static int
ext_mkdir(struct vnode *dvp, char *dirname, mode_t mode)
{
    ext_debug("mkdir %s under i-node %li\n", dirname, dvp->v_ino);

    uint32_t len = strlen(dirname);
    if (len > NAME_MAX || len > EXT4_DIRECTORY_FILENAME_LEN) {
        return ENAMETOOLONG;
    }

    if (!S_ISDIR(mode))
        return EINVAL;

    return ext_dir_link(dvp, dirname, EXT4_DE_DIR, nullptr, nullptr);
}

static int
ext_rmdir(vnode_t *dvp, vnode_t *vp, char *name)
{
    ext_debug("rmdir\n");
    return ext_dir_remove_entry(dvp, vp, name);
}

static int
ext_getattr(vnode_t *vp, vattr_t *vap)
{
    ext_debug("Getting attributes at i-node:%ld\n", vp->v_ino);
    struct ext4_fs *fs = (struct ext4_fs *)vp->v_mount->m_data;

    auto_inode_ref inode_ref(fs, vp->v_ino);
    if (inode_ref._r != EOK) {
        return inode_ref._r;
    }

    vap->va_mode = ext4_inode_get_mode(&fs->sb, inode_ref._ref.inode);

    uint32_t i_type = ext4_inode_type(&fs->sb, inode_ref._ref.inode);
    if (i_type == EXT4_INODE_MODE_DIRECTORY) {
       vap->va_type = VDIR;
    } else if (i_type == EXT4_INODE_MODE_FILE) {
        vap->va_type = VREG;
    } else if (i_type == EXT4_INODE_MODE_SOFTLINK) {
        vap->va_type = VLNK;
    }

    vap->va_nodeid = vp->v_ino;
    vap->va_size = ext4_inode_get_size(&fs->sb, inode_ref._ref.inode);
    ext_debug("getattr: va_size:%ld\n", vap->va_size);

    vap->va_atime.tv_sec = ext4_inode_get_access_time(inode_ref._ref.inode);
    vap->va_mtime.tv_sec = ext4_inode_get_modif_time(inode_ref._ref.inode);
    vap->va_ctime.tv_sec = ext4_inode_get_change_inode_time(inode_ref._ref.inode);

    //auto *fsid = &vnode->v_mount->m_fsid; //TODO
    //attr->va_fsid = ((uint32_t)fsid->__val[0]) | ((dev_t) ((uint32_t)fsid->__val[1]) << 32);

    return (EOK);
}

static int
ext_setattr(vnode_t *vp, vattr_t *vap)
{
    ext_debug("setattr\n");
    struct ext4_fs *fs = (struct ext4_fs *)vp->v_mount->m_data;

    auto_write_back wb(fs);
    auto_inode_ref inode_ref(fs, vp->v_ino);
    if (inode_ref._r != EOK) {
        return inode_ref._r;
    }

    if (vap->va_mask & AT_ATIME) {
        ext4_inode_set_access_time(inode_ref._ref.inode, vap->va_atime.tv_sec);
        inode_ref._ref.dirty = true;
    }

    if (vap->va_mask & AT_CTIME) {
        ext4_inode_set_change_inode_time(inode_ref._ref.inode, vap->va_ctime.tv_sec);
        inode_ref._ref.dirty = true;
    }

    if (vap->va_mask & AT_MTIME) {
        ext4_inode_set_modif_time(inode_ref._ref.inode, vap->va_mtime.tv_sec);
        inode_ref._ref.dirty = true;
    }

    if (vap->va_mask & AT_MODE) {
        ext4_inode_set_mode(&fs->sb, inode_ref._ref.inode, vap->va_mode);
        inode_ref._ref.dirty = true;
    }

    return (EOK);
}

static int
ext_truncate(struct vnode *vp, off_t new_size)
{
    ext_debug("truncate\n");
    struct ext4_fs *fs = (struct ext4_fs *)vp->v_mount->m_data;
    auto_write_back wb(fs);
    return ext_trunc_inode(fs, vp->v_ino, new_size);
}

static int
ext_link(vnode_t *tdvp, vnode_t *svp, char *name)
{
    ext_debug("link\n");
    uint32_t len = strlen(name);
    if (len > NAME_MAX || len > EXT4_DIRECTORY_FILENAME_LEN) {
        return ENAMETOOLONG;
    }

    uint32_t source_link_no = svp->v_ino;
    return ext_dir_link(tdvp, name, EXT4_DE_REG_FILE, &source_link_no, nullptr);
}

static int
ext_arc(vnode_t *vp, struct file* fp, uio_t *uio)
{
    kprintf("[ext4] arc\n");
    return (EINVAL);
}

static int
ext_fallocate(vnode_t *vp, int mode, loff_t offset, loff_t len)
{
    kprintf("[ext4] fallocate\n");
    return (EINVAL);
}

static int
ext_readlink(vnode_t *vp, uio_t *uio)
{
    ext_debug("readlink\n");
    if (vp->v_type != VLNK) {
        return EINVAL;
    }
    if (uio->uio_offset < 0) {
        return EINVAL;
    }
    if (uio->uio_resid == 0) {
        return 0;
    }

    struct ext4_fs *fs = (struct ext4_fs *)vp->v_mount->m_data;

    auto_inode_ref inode_ref(fs, vp->v_ino);
    if (inode_ref._r != EOK) {
        return inode_ref._r;
    }

    uint64_t fsize = ext4_inode_get_size(&fs->sb, inode_ref._ref.inode);
    if (fsize < sizeof(inode_ref._ref.inode->blocks)
             && !ext4_inode_get_blocks_count(&fs->sb, inode_ref._ref.inode)) {

        char *content = (char *)inode_ref._ref.inode->blocks;
        return uiomove(content, fsize, uio);
    } else {
        uint32_t block_size = ext4_sb_get_block_size(&fs->sb);
        void *buf = malloc(block_size);
        size_t read_count = 0;
        int ret = ext_internal_read(fs, &inode_ref._ref, uio->uio_offset, buf, fsize, &read_count);
        if (ret) {
            kprintf("[ext_readlink] Error reading data\n");
            free(buf);
            return ret;
        }

        ret = uiomove(buf, read_count, uio);
        free(buf);
        return ret;
    }
}

static int
ext_fsymlink_set(struct ext4_fs *fs, uint32_t inode_no, const void *buf, uint32_t size)
{
    uint32_t block_size = ext4_sb_get_block_size(&fs->sb);
    if (size > block_size) {
        return EINVAL;
    }

    auto_inode_ref inode_ref(fs, inode_no);
    if (inode_ref._r != EOK) {
        return inode_ref._r;
    }

    /*If the size of symlink is smaller than 60 bytes*/
    if (size < sizeof(inode_ref._ref.inode->blocks)) {
        memset(inode_ref._ref.inode->blocks, 0, sizeof(inode_ref._ref.inode->blocks));
        memcpy(inode_ref._ref.inode->blocks, buf, size);
        ext4_inode_clear_flag(inode_ref._ref.inode, EXT4_INODE_FLAG_EXTENTS);
    } else {
        ext4_fs_inode_blocks_init(fs, &inode_ref._ref);

        uint32_t sblock;
        ext4_fsblk_t fblock;
        int r = ext4_fs_append_inode_dblk(&inode_ref._ref, &fblock, &sblock);
        if (r != EOK)
            return r;

        uint64_t off = fblock * block_size;
        r = ext4_block_writebytes(fs->bdev, off, buf, size);
        if (r != EOK)
            return r;
    }

    ext4_inode_set_size(inode_ref._ref.inode, size);
    inode_ref._ref.dirty = true;

    return EOK;
}

static int
ext_symlink(vnode_t *dvp, char *name, char *link)
{
    ext_debug("symlink\n");
    struct ext4_fs *fs = (struct ext4_fs *)dvp->v_mount->m_data;
    auto_write_back wb(fs);
    uint32_t inode_no_created;
    int r = ext_dir_link(dvp, name, EXT4_DE_SYMLINK, nullptr, &inode_no_created);
    if (r == EOK ) {
       return ext_fsymlink_set(fs, inode_no_created, link, strlen(link));
    }
    return r;
}

#define ext_seek        ((vnop_seek_t)vop_nullop)
#define ext_inactive    ((vnop_inactive_t)vop_nullop)

struct vnops ext_vnops = {
    ext_open,       /* open */
    ext_close,      /* close */
    ext_read,       /* read */
    ext_write,      /* write */
    ext_seek,       /* seek */
    ext_ioctl,      /* ioctl */
    ext_fsync,      /* fsync */
    ext_readdir,    /* readdir */
    ext_lookup,     /* lookup */
    ext_create,     /* create */
    ext_remove,     /* remove */
    ext_rename,     /* rename */
    ext_mkdir,      /* mkdir */
    ext_rmdir,      /* rmdir */
    ext_getattr,    /* getattr */
    ext_setattr,    /* setattr */
    ext_inactive,   /* inactive */
    ext_truncate,   /* truncate */
    ext_link,       /* link */
    ext_arc,        /* arc */
    ext_fallocate,  /* fallocate */
    ext_readlink,   /* read link */
    ext_symlink,    /* symbolic link */
};

