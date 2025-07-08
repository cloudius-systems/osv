/*
 * Copyright (C) 2024 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

extern "C" {
#define USE_C_INTERFACE 1
#include <osv/device.h>
#include <osv/bio.h>
#include <osv/prex.h>
#include <osv/vnode.h>
#include <osv/mount.h>
#include <osv/debug.h>

void* alloc_contiguous_aligned(size_t size, size_t align);
void free_contiguous_aligned(void* p);
}

#include <ext4_blockdev.h>
#include <ext4_debug.h>
#include <ext4_fs.h>
#include <ext4_super.h>

#include <cstdlib>
#include <cstddef>
#include <cstdio>

//#define CONF_debug_ext 1
#if CONF_debug_ext
#define ext_debug(format,...) kprintf("[ext4] " format, ##__VA_ARGS__)
#else
#define ext_debug(...)
#endif

extern "C" bool is_linear_mapped(const void *addr);

int ext_init(void) { return 0;}

static int blockdev_open(struct ext4_blockdev *bdev)
{
    return EOK;
}

static int blockdev_bread_or_write(struct ext4_blockdev *bdev, void *buf, uint64_t blk_id, uint32_t blk_cnt, bool read)
{
    struct bio *bio = alloc_bio();
    if (!bio)
        return ENOMEM;

    bio->bio_cmd = read ? BIO_READ : BIO_WRITE;
    bio->bio_dev = (struct device*)bdev->bdif->p_user;
    bio->bio_offset = blk_id * bdev->bdif->ph_bsize;
    bio->bio_bcount = blk_cnt * bdev->bdif->ph_bsize;

    if (!is_linear_mapped(buf)) {
        bio->bio_data = alloc_contiguous_aligned(bio->bio_bcount, alignof(std::max_align_t));
        if (!read) {
            memcpy(bio->bio_data, buf, bio->bio_bcount);
        }
    } else {
        bio->bio_data = buf;
    }

    bio->bio_dev->driver->devops->strategy(bio);
    int error = bio_wait(bio);

    ext_debug("%s %ld bytes at offset %ld to %p with error:%d\n", read ? "Read" : "Wrote",
        bio->bio_bcount, bio->bio_offset, bio->bio_data, error);

    if (!is_linear_mapped(buf)) {
        if (read && !error) {
            memcpy(buf, bio->bio_data, bio->bio_bcount);
        }
        free_contiguous_aligned(bio->bio_data);
    }
    destroy_bio(bio);

    return error;
}

static int blockdev_bread(struct ext4_blockdev *bdev, void *buf, uint64_t blk_id, uint32_t blk_cnt)
{
    return blockdev_bread_or_write(bdev, buf, blk_id, blk_cnt, true);
}

static int blockdev_bwrite(struct ext4_blockdev *bdev, const void *buf,
                           uint64_t blk_id, uint32_t blk_cnt)
{
    return blockdev_bread_or_write(bdev, const_cast<void *>(buf), blk_id, blk_cnt, false);
}

static int blockdev_close(struct ext4_blockdev *bdev)
{
    return EOK;
}

EXT4_BLOCKDEV_STATIC_INSTANCE(ext_blockdev, 512, 0, blockdev_open,
                              blockdev_bread, blockdev_bwrite, blockdev_close, 0, 0);

static struct ext4_fs ext_fs;
static struct ext4_bcache ext_block_cache;
extern struct vnops ext_vnops;

static mutex_t ext_inode_alloc_mutex;
static void ext_inode_alloc_lock()
{
    mutex_lock(&ext_inode_alloc_mutex);
}

static void ext_inode_alloc_unlock()
{
    mutex_unlock(&ext_inode_alloc_mutex);
}

static mutex_t ext_block_alloc_mutex;
static void ext_block_alloc_lock()
{
    mutex_lock(&ext_block_alloc_mutex);
}
static void ext_block_alloc_unlock()
{
    mutex_unlock(&ext_block_alloc_mutex);
}

static mutex_t ext_bcache_mutex;
static void ext_bcache_lock()
{
    mutex_lock(&ext_bcache_mutex);
}

static void ext_bcache_unlock()
{
    mutex_unlock(&ext_bcache_mutex);
}

#define META_BLOCK_DEV_CACHE_SIZE 32

static int
ext_mount(struct mount *mp, const char *dev, int flags, const void *data)
{
    struct device *device;

    const char *dev_name = dev + 5;
    ext_debug("[ext4] Trying to open device: [%s]\n", dev_name);
    int error = device_open(dev_name, DO_RDWR, &device);

    if (error) {
        kprintf("[ext4] Error opening device!\n");
        return error;
    }

    mutex_init(&ext_inode_alloc_mutex);
    ext_fs.inode_alloc_lock = ext_inode_alloc_lock;
    ext_fs.inode_alloc_unlock = ext_inode_alloc_unlock;

    mutex_init(&ext_block_alloc_mutex);
    ext_fs.block_alloc_lock = ext_block_alloc_lock;
    ext_fs.block_alloc_unlock = ext_block_alloc_unlock;

    mutex_init(&ext_bcache_mutex);
    ext_fs.bcache_lock = ext_bcache_lock;
    ext_fs.bcache_unlock = ext_bcache_unlock;

    ext4_dmask_set(DEBUG_ALL);
    //
    // Save a reference to the filesystem
    mp->m_dev = device;
    ext_blockdev.bdif->p_user = device;
    ext_blockdev.part_offset = 0;
    ext_blockdev.part_size = device->size;
    ext_blockdev.bdif->ph_bcnt = ext_blockdev.part_size / ext_blockdev.bdif->ph_bsize;

    ext_debug("Trying to mount ext4 on device: [%s] with size:%ld\n", dev_name, device->size);
    int r = ext4_block_init(&ext_blockdev);
    if (r != EOK) {
        return r;
    }

    r = ext4_fs_init(&ext_fs, &ext_blockdev, false);
    if (r != EOK) {
        ext4_block_fini(&ext_blockdev);
        return r;
    }

    uint32_t bsize = ext4_sb_get_block_size(&ext_fs.sb);
    ext4_block_set_lb_size(&ext_blockdev, bsize);

    r = ext4_bcache_init_dynamic(&ext_block_cache, META_BLOCK_DEV_CACHE_SIZE, bsize);
    if (r != EOK) {
        ext4_block_fini(&ext_blockdev);
        return r;
    }

    if (bsize != ext_block_cache.itemsize)
        return ENOTSUP;

    /*Bind block cache to block device*/
    r = ext4_block_bind_bcache(&ext_blockdev, &ext_block_cache);
    if (r != EOK) {
        ext4_bcache_cleanup(&ext_block_cache);
        ext4_block_fini(&ext_blockdev);
        ext4_bcache_fini_dynamic(&ext_block_cache);
        return r;
    }

    ext_blockdev.fs = &ext_fs;
    mp->m_data = &ext_fs;
    mp->m_root->d_vnode->v_ino = EXT4_INODE_ROOT_INDEX;
    mp->m_root->d_vnode->v_type = VDIR;
    //Enable write-back cache to optimize reading and writing of metadata blocks
    ext4_block_cache_write_back(ext_fs.bdev, 1);

    kprintf("[ext4] Mounted ext with i-node size:%d and block size:%d on device: [%s]\n",
        ext4_get16(&ext_fs.sb, inode_size), bsize, dev_name);
    return r;
}

static int
ext_unmount(struct mount *mp, int flags)
{
    int r = ext4_fs_fini(&ext_fs);
    if (r == EOK) {
        ext4_bcache_cleanup(&ext_block_cache);
        ext4_bcache_fini_dynamic(&ext_block_cache);
    }

    ext4_block_fini(&ext_blockdev);
    r = device_close((struct device*)ext_blockdev.bdif->p_user);
    kprintf("[ext4] Unmounted ext filesystem!\n");
    return r;
}

static int
ext_sync(struct mount *mp)
{
    struct ext4_fs *fs = (struct ext4_fs *)mp->m_data;
    fs->bcache_lock();
    auto ret = ext4_block_cache_flush(fs->bdev);
    fs->bcache_unlock();
    return ret;
}

static int
ext_statfs(struct mount *mp, struct statfs *statp)
{
    ext_debug("statfs\n");
    struct ext4_fs *fs = (struct ext4_fs *)mp->m_data;
    statp->f_bsize = ext4_sb_get_block_size(&fs->sb);

    statp->f_blocks = ext4_sb_get_blocks_cnt(&fs->sb);
    statp->f_bfree = ext4_sb_get_free_blocks_cnt(&fs->sb);
    statp->f_bavail = ext4_sb_get_free_blocks_cnt(&fs->sb);

    statp->f_ffree = ext4_get32(&fs->sb, free_inodes_count);
    statp->f_files = ext4_get32(&fs->sb, inodes_count);

    statp->f_namelen = EXT4_DIRECTORY_FILENAME_LEN;
    statp->f_type = EXT4_SUPERBLOCK_MAGIC;

    statp->f_fsid = mp->m_fsid; /* File system identifier */
    return EOK;
}

// We are relying on vfsops structure defined in kernel
extern struct vfsops ext_vfsops;

// Overwrite "null" vfsops structure fields with "real"
// functions upon loading libext.so shared object
void __attribute__((constructor)) initialize_vfsops() {
    ext_vfsops.vfs_mount = ext_mount;
    ext_vfsops.vfs_unmount = ext_unmount;
    ext_vfsops.vfs_sync = ext_sync;
    ext_vfsops.vfs_vget = ((vfsop_vget_t)vfs_nullop);
    ext_vfsops.vfs_statfs = ext_statfs;
    ext_vfsops.vfs_vnops = &ext_vnops;
    ext_debug("libext loaded!\n");
}

asm(".pushsection .note.osv-mlock, \"a\"; .long 0, 0, 0; .popsection");
