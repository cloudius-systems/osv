// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2013, Cloudius Systems. All rights reserved.
 * Copyright (c) 2026, OSv contributors. All rights reserved.
 *
 * ZFS initialization for OSv.
 *
 * This file provides the entry point for initializing the ZFS
 * subsystem in OSv. It replaces the old zfs_init.c and integrates
 * with the OpenZFS 2.x initialization framework.
 *
 * Architecture:
 *   - libsolaris.so is dynamically loaded into the OSv image.
 *   - The ELF constructor zfs_module_init() runs automatically on load.
 *   - It registers osv_zfs_ioctl() as the ioctl dispatcher for /dev/zfs,
 *     then calls zfs_kmod_init() to initialize the full ZFS stack.
 *   - User-space ZFS tools (libzfs, zpool, zfs) open /dev/zfs and issue
 *     ioctl(2) calls which are dispatched through drivers/zfs.cc to
 *     osv_zfs_ioctl() -> zfsdev_ioctl_common().
 */

#include <sys/types.h>
#include <sys/zfs_context.h>
#include <sys/zfs_vfsops.h>
#include <sys/zfs_ioctl.h>
#include <sys/zfs_ioctl_impl.h>
#include <sys/zvol.h>
#include <sys/spa.h>
#include <sys/zfs_quota.h>
#include <osv/mount.h>

/*
 * Thread-local variable for ZFS fsyncer.
 */
__thread void *zfs_fsyncer_key;

/*
 * OSv ARC shrinker wrappers.
 *
 * OpenZFS 2.x does not use the BSD vm_lowmem eventhandler mechanism that
 * the old BSD-ZFS port used to register arc_lowmem() / arc_sized_adjust()
 * with OSv's memory reclaimer.  We bridge the gap here by providing thin
 * wrappers around arc_reduce_target_size_noshrink() and registering them via
 * register_shrinker_arc_funs() after arc_init() has run inside
 * zfs_kmod_init().  Without this, OSv's memory reclaimer cannot shrink the
 * ARC under pressure, causing alloc_phys_contiguous_aligned() to block
 * forever waiting for memory that never gets freed - a deadlock in the ZIO
 * pipeline during TXG sync.
 */
extern uint64_t arc_reduce_target_size_noshrink(uint64_t to_free);
/*
 * Use arc_all_memory() (a function call via PLT within libsolaris.so) rather
 * than referencing arc_c directly.  arc_c is compiled with -fvisibility=hidden
 * and has no entry in libsolaris.so's dynamic symbol table; an 'extern'
 * declaration without a matching hidden attribute causes the compiler to emit
 * a GOT-indirect reference that the dynamic linker resolves externally.
 * Because arc_c is not exported by loader.elf the GOT slot is filled with
 * missing_symbols_page_addr, which crashes on first access.
 */
extern uint64_t arc_all_memory(void);

/* Called by the shrinker in "hard" mode (evict aggressively). */
static size_t
osv_arc_lowmem(void *arg, int howto)
{
	uint64_t to_free = arc_all_memory() / 4;
	if (to_free < (16UL << 20))
		to_free = 16UL << 20;
	return ((size_t)arc_reduce_target_size_noshrink(to_free));
}

/* Called by the shrinker in "soft" mode (reclaim a specific amount). */
static size_t
osv_arc_sized_adjust(int64_t to_reclaim)
{
	if (to_reclaim <= 0)
		return (0);
	return ((size_t)arc_reduce_target_size_noshrink((uint64_t)to_reclaim));
}

extern void register_shrinker_arc_funs(
    size_t (*)(void *, int),
    size_t (*)(int64_t));

/*
 * osv_arc_dbuf_rele() is defined in zfs_vnops_os.c; osv_pagecache_register_arc_rele()
 * lives in loader.elf (core/pagecache.cc).  Registering the former with the
 * latter lets the page cache release a borrowed ARC dbuf hold when a shared
 * mmap page (installed by zfs_vop_cache()'s page-sharing path) is dropped.
 */
extern void osv_arc_dbuf_rele(void *db);
extern void osv_pagecache_register_arc_rele(void (*rele)(void *db));

/*
 * The real ZFS VFS operations, defined in zfs_vfsops.c.
 */
extern struct vfsops zfs_osv_vfsops;

/*
 * zfs_update_vfsops() patches the live zfs_vfsops struct in place
 * (defined in fs/zfs/zfs_null_vfsops.cc).
 */
extern void zfs_update_vfsops(struct vfsops *vfsops);

/*
 * register_osv_zfs_ioctl() sets the function pointer in drivers/zfs.cc
 * that backs the /dev/zfs ioctl handler.
 */
extern void register_osv_zfs_ioctl(int (*fun)(unsigned long, void *));

/*
 * opensolaris_load() initializes the OpenSolaris compat subsystem.
 * Sets nsec_per_tick (= NANOSEC/hz = 1000000), cpu_lock, solaris_cpu[].
 * In OSv, SYSINIT is a no-op, so we call this explicitly.
 */
extern void opensolaris_load(void *dummy);

/*
 * callb_init() initializes the callb mechanism used by ZFS threads
 * (CALLB_CPR_INIT in l2arc_feed_thread, txg_thread, etc.).
 * In OSv, SYSINIT is a no-op, so we call this explicitly.
 */
extern void callb_init(void *dummy);

/*
 * system_taskq_init() creates the system taskq (from BSD compat layer).
 * Must be called before ZFS can dispatch work items.
 */
extern void system_taskq_init(void *arg);

/*
 * zfs_znode_init() initializes the znode slab cache.
 *
 * The real zfs_init() defined in zfs_vfsops.c calls zfs_znode_init(), but
 * on OSv the dummy zfs_init() in loader.elf (zfs_null_vfsops.cc) shadows it
 * in the global symbol table.  We call zfs_znode_init() directly to ensure
 * the znode slab is set up before any ZFS operations.
 */
extern void zfs_znode_init(void);

/*
 * zfs_driver_initialized is defined in fs/zfs/zfs_null_vfsops.cc and
 * exported from the kernel (loader.elf).  We check it to avoid
 * double-initialization.
 */
extern bool zfs_driver_initialized;

/*
 * osv_zfs_ioctl -- the ioctl dispatcher for /dev/zfs on OSv.
 *
 * Called from drivers/zfs.cc when user-space issues ioctl(zfs_fd, req, buf).
 * Extracts the ZFS IOC vector number and delegates to zfsdev_ioctl_common(),
 * which is the common entry point shared by all OS ports.
 *
 * req   -- raw ioctl request number (ZFS_IOC_* from sys/fs/zfs.h).
 *          The low 8 bits are the vector index (ZFS_IOC macro).
 * buffer -- pointer to zfs_cmd_t passed from user-space (no copy needed
 *           in OSv since there is no user/kernel address split).
 */
static int
osv_zfs_ioctl(unsigned long req, void *buffer)
{
	/*
	 * Extract the vector index from the ioctl request.
	 * On FreeBSD ZFS_IOC_FIRST=0, on Linux/OSv ZFS_IOC_FIRST=('Z'<<8).
	 * In both cases (req & 0xff) yields the correct 0-based vector index
	 * since the base is always aligned to a multiple of 256.
	 *
	 * FKIOCTL tells zfsdev_ioctl_common() that the buffer is already
	 * in unified address space -- no copyin/copyout needed.
	 */
	return ((int)zfsdev_ioctl_common((uint_t)(req & 0xff),
	    (zfs_cmd_t *)buffer, FKIOCTL));
}

/*
 * zfs_initialize -- ELF constructor, runs automatically when libsolaris.so
 * is loaded by the OSv dynamic linker.
 *
 * Initialization order:
 *   1. system_taskq_init()   -- BSD compat taskq (used by ZFS internally)
 *   2. register_osv_zfs_ioctl() -- wire up /dev/zfs ioctl dispatch
 *   3. zfs_kmod_init()       -- full OpenZFS subsystem init (spa, dmu, arc,
 *                               zvol, ioctl table, rrw_tsd, ...)
 *   4. dmu_objset_register_type() -- register ZFS objset type callback
 *   5. zfs_update_vfsops()   -- replace null vfsops with real ZFS vfsops
 */
void __attribute__((constructor))
zfs_initialize(void)
{
	int error;

	if (zfs_driver_initialized) {
		printf("zfs: driver already initialized\n");
		return;
	}

	/*
	 * In OSv, SYSINIT() is a no-op macro, so BSD compat subsystems that
	 * register via SYSINIT are never auto-initialized.  Call them explicitly
	 * in dependency order before any ZFS code runs.
	 *
	 * 1. opensolaris_load: sets nsec_per_tick = NANOSEC/hz (1000000 ns).
	 *    Without this, ddi_get_lbolt() divides by zero the moment any ZFS
	 *    thread calls it (l2arc_feed_thread, txg threads, zthr threads...).
	 *
	 * 2. callb_init: initializes the callb table and its mutexes.
	 *    Required by CALLB_CPR_INIT used in l2arc_feed_thread et al.
	 */
	opensolaris_load(NULL);
	callb_init(NULL);

	/* BSD compat taskq must exist before spa_init() dispatches work. */
	system_taskq_init(NULL);

	/*
	 * Initialize freemem before arc_init() so that arc_available_memory()
	 * doesn't read from the zero-initialized BSS and conclude the system
	 * is under extreme memory pressure.  physmem is exported from
	 * loader.elf; using physmem/4 is conservative but avoids triggering
	 * the ARC low-memory shrinker on startup.
	 */
	{
		extern unsigned long freemem;
		extern unsigned long physmem;
		freemem = physmem / 4;
	}

	/*
	 * Low-memory tuning: must be set before zfs_kmod_init() → spa_init()
	 * → arc_init() consults them.
	 *
	 * zfs_txg_timeout: commit dirty data to disk every 2 seconds instead
	 * of the default 5.  Reduces peak dirty-data accumulation under memory
	 * pressure without meaningfully hurting throughput in OSv's single-app
	 * model.  Workloads that issue their own fsync/fdatasync are unaffected.
	 *
	 * zfs_dirty_data_max_percent: cap dirty data at 5% of physmem (6.4 MiB
	 * at 128 MiB RAM) instead of the default 10% (12.8 MiB).  This saves
	 * ~6 MiB of peak physical memory during TXG sync, which is significant
	 * at 128 MiB.  The risk of ERESTART from dirty-data throttling is low
	 * when used with lz4 compression (compressed writes are smaller) and
	 * with the 2-second TXG timeout above (shorter batches per commit).
	 *
	 * arc_c_max is set by arc_default_max() in arc_os.c using a tiered
	 * formula that caps the ARC at 1/6 of RAM (≈21 MiB) for 128–256 MiB
	 * systems.  No additional cap is needed here.
	 */
	{
		extern uint_t zfs_txg_timeout;
		zfs_txg_timeout = 2;
	}
	{
		extern uint_t zfs_dirty_data_max_percent;
		zfs_dirty_data_max_percent = 5;
	}

	/*
	 * Enable ZFS recovery mode for OSv.
	 *
	 * zfs_panic_recover() calls CE_PANIC by default, which aborts the
	 * entire unikernel on any ZFS metadata inconsistency.  On OSv the
	 * link-count bookkeeping inside zfs_link_destroy() can see transient
	 * mismatches when a directory znode is loaded via zfs_zget() without
	 * going through the VFS vget() path (z_vnode == NULL), causing
	 * zp_is_dir to be computed as 0 instead of 1.  This triggers a
	 * spurious panic during the rmdir() inside libzfs remove_mountpoint().
	 *
	 * Setting zfs_recover = B_TRUE converts CE_PANIC → CE_WARN in
	 * zfs_panic_recover(), so the inconsistency is logged but the
	 * unikernel continues.  This is the canonical OpenZFS recovery
	 * mechanism (see zfs_recover in spa_misc.c).
	 *
	 * The root cause (z_vnode not set on znodes from zfs_zget) should
	 * be fixed properly in zfs_vop_rmdir / zfs_zget, but until then
	 * this prevents fatal aborts on dataset teardown.
	 */
	{
		extern int zfs_recover;
		zfs_recover = B_TRUE;
	}

	/* Wire up the /dev/zfs ioctl path. */
	register_osv_zfs_ioctl(osv_zfs_ioctl);

	/*
	 * Initialize the Illumos Crypto Provider (ICP) before any ZFS crypto
	 * operations.  icp_init() sets up the KCF mechanism tables, provider
	 * table, scheduler, and registers AES + SHA-2 algorithm providers.
	 * Without this, zio_crypt_key_init() → hkdf_sha512 → crypto_mac()
	 * fails because no SHA-512-HMAC provider is registered.
	 *
	 * The Linux port calls icp_init() in zfs_ioctl_os.c; we mirror that
	 * here since OSv does not use the Linux ioctl layer.
	 */
	extern int icp_init(void);
	error = icp_init();
	if (error != 0) {
		printf("ZFS: icp_init() failed, rc = %d\n", error);
		return;
	}

	/*
	 * zcommon_init() is normally registered via module_init_early() which
	 * is a no-op on OSv.  Call it explicitly to run the fletcher4 benchmark
	 * and populate fletcher_4_fastest_impl before zfs_kmod_init() needs it.
	 */
	extern int zcommon_init(void);
	error = zcommon_init();
	if (error != 0) {
		printf("ZFS: zcommon_init() failed, rc = %d\n", error);
		return;
	}

	error = zfs_kmod_init();
	if (error != 0) {
		printf("ZFS: zfs_kmod_init() failed, rc = %d\n", error);
		return;
	}

	/*
	 * Register OpenZFS ARC with OSv's memory reclaimer.
	 *
	 * arc_init() ran inside zfs_kmod_init() above, so arc_c is now valid.
	 * register_shrinker_arc_funs() sets the function pointers and calls
	 * new arc_shrinker() to register with the OSv memory-pressure subsystem.
	 * Without this the reclaimer cannot shrink the ARC when physical memory
	 * is exhausted, causing alloc_phys_contiguous_aligned() to block forever
	 * inside the ZIO pipeline - a deadlock during TXG sync.
	 */
	register_shrinker_arc_funs(osv_arc_lowmem, osv_arc_sized_adjust);

	/*
	 * Register the ARC dbuf rele callback so the page cache can release
	 * holds taken by zfs_vop_cache()'s mmap page-sharing path.
	 */
	osv_pagecache_register_arc_rele(osv_arc_dbuf_rele);

	zfs_znode_init();

	dmu_objset_register_type(DMU_OST_ZFS, zpl_get_file_info);

	zfs_update_vfsops(&zfs_osv_vfsops);

	zfs_driver_initialized = true;
	printf("ZFS: OpenZFS " SPA_VERSION_STRING " initialized\n");
}

/*
 * zfs_shutdown -- called if libsolaris.so is ever unloaded (rare on OSv).
 */
void
zfs_shutdown(void)
{
	zfs_kmod_fini();
}

/*
 * This note causes the OSv dynamic linker to pre-fault (mlock) all segments
 * of libsolaris.so on load, preventing page faults inside ZFS code paths
 * that run with locks held (which would deadlock if they faulted).
 */
asm(".pushsection .note.osv-mlock, \"a\"; .long 0, 0, 0; .popsection");
