/*
 * Copyright (C) 2021 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <stddef.h>
#include <stdio.h>
#include <osv/mount.h>
#include <osv/debug.h>
#include <sys/arc.h>

//This file gets linked as part of libsolaris.so to
//provide an INIT function to initialize ZFS filesystem
//code

extern void system_taskq_init(void *arg);
extern void opensolaris_load(void *arg);
extern void callb_init(void *arg);

extern int osv_zfs_ioctl(unsigned long req, void* buffer);
//The function below is part of kernel and is used to
//register osv_zfs_ioctl() as a callback
extern void register_osv_zfs_ioctl( int (*osv_zfs_ioctl_fun)(unsigned long, void*));

extern size_t arc_lowmem(void *arg, int howto);
extern size_t arc_sized_adjust(long to_reclaim);
//The function below is part of kernel and is used to
//register arc_lowmem() and arc_sized_adjust() as callbacks
extern void register_shrinker_arc_funs(
    size_t (*_arc_lowmem_fun)(void *, int),
    size_t (*_arc_sized_adjust_fun)(long));

extern void arc_unshare_buf(arc_buf_t*);
extern void arc_share_buf(arc_buf_t*);
extern void arc_buf_accessed(const uint64_t[4]);
extern void arc_buf_get_hashkey(arc_buf_t*, uint64_t[4]);
//The function below is part of kernel and is used to
//register for functions above - arc_*() - as callbacks
extern void register_pagecache_arc_funs(
    void (*_arc_unshare_buf_fun)(arc_buf_t*),
    void (*_arc_share_buf_fun)(arc_buf_t*),
    void (*_arc_buf_accessed_fun)(const uint64_t[4]),
    void (*_arc_buf_get_hashkey_fun)(arc_buf_t*, uint64_t[4]));

extern struct vfsops zfs_vfsops;
//The function below is part of kernel and is used to
//update ZFS vfsops in the vfssw configuration struct
extern void zfs_update_vfsops(struct vfsops* _vfsops);

extern void start_pagecache_access_scanner();

extern int zfs_init(void);

//This init function gets called on loading of libsolaris.so
//and it initializes all necessary resources (threads, etc) used by the code in
//libsolaris.so. This initialization is necessary before ZFS can be mounted.
void __attribute__((constructor)) zfs_initialize(void) {
    // These 3 functions used to be called at the end of bsd_init()
    // and are intended to initialize various resources, mainly thread pools
    // (threads named 'system_taskq_*' and 'solthread-0x*')
    opensolaris_load(NULL);
    callb_init(NULL);
    system_taskq_init(NULL);

    //Register osv_zfs_ioctl() as callback in drivers/zfs.cc
    register_osv_zfs_ioctl(&osv_zfs_ioctl);
    //Register arc_lowmem() and arc_sized_adjust() as callbacks in arc_shrinker
    //implemented as part of bsd/porting/shrinker.cc
    register_shrinker_arc_funs(&arc_lowmem, &arc_sized_adjust);
    //Register arc_unshare_buf(), arc_share_buf(), arc_buf_accessed() and arc_buf_get_hashkey()
    //as callbacks in the page cache layer implemented in core/pagecache.cc
    register_pagecache_arc_funs(&arc_unshare_buf, &arc_share_buf, &arc_buf_accessed, &arc_buf_get_hashkey);

    //Register vfsops and vnops ...
    zfs_update_vfsops(&zfs_vfsops);
    //Start ZFS access scanner (part of pagecache)
    start_pagecache_access_scanner();

    //Finally call zfs_init() which is what would been normally called by vfs_init()
    //The dummy zfs_init() defined in kernel does not do anything so
    //we have to call the real one here as a last step after everything else above
    //was called to initialize various ZFS resources and register relevant callback
    //functions in the kernel
    zfs_init();

    debug("zfs: driver has been initialized!\n");
}

//This is important to make sure that OSv dynamic linker will
//pre-fault (populate) all segments of libsolaris.so on load
//before any of its code is executed. This makes it so that ZFS
//code does not trigger any faults which is important
//when handling map() or unmap() on ZFS files for example.
//Without it we would encounter deadlocks in such scenarios.
asm(".pushsection .note.osv-mlock, \"a\"; .long 0, 0, 0; .popsection");
