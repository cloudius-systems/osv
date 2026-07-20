# OpenZFS source file mapping for OSv
#
# This file defines the source objects for building libsolaris.so
# from OpenZFS 2.4.1 instead of the old illumos/FreeBSD ZFS port.
#
# Generated from the actual source files in external/openzfs/module/.
# Paths are relative to the repository root.
#
# Usage in Makefile:
#   include bsd/sys/cddl/openzfs_sources.mk

OPENZFS := external/openzfs

# ============================================================
# Platform-independent ZFS code (from module/zfs/)
# ============================================================
openzfs-zfs :=
openzfs-zfs += $(OPENZFS)/module/zfs/abd.o
openzfs-zfs += $(OPENZFS)/module/zfs/aggsum.o
openzfs-zfs += $(OPENZFS)/module/zfs/arc.o
openzfs-zfs += $(OPENZFS)/module/zfs/blake3_zfs.o
openzfs-zfs += $(OPENZFS)/module/zfs/blkptr.o
openzfs-zfs += $(OPENZFS)/module/zfs/bplist.o
openzfs-zfs += $(OPENZFS)/module/zfs/bpobj.o
openzfs-zfs += $(OPENZFS)/module/zfs/bptree.o
openzfs-zfs += $(OPENZFS)/module/zfs/bqueue.o
openzfs-zfs += $(OPENZFS)/module/zfs/brt.o
openzfs-zfs += $(OPENZFS)/module/zfs/btree.o
openzfs-zfs += $(OPENZFS)/module/zfs/dataset_kstats.o
openzfs-zfs += $(OPENZFS)/module/zfs/dbuf.o
openzfs-zfs += $(OPENZFS)/module/zfs/dbuf_stats.o
openzfs-zfs += $(OPENZFS)/module/zfs/ddt.o
openzfs-zfs += $(OPENZFS)/module/zfs/ddt_log.o
openzfs-zfs += $(OPENZFS)/module/zfs/ddt_stats.o
openzfs-zfs += $(OPENZFS)/module/zfs/ddt_zap.o
openzfs-zfs += $(OPENZFS)/module/zfs/dmu.o
openzfs-zfs += $(OPENZFS)/module/zfs/dmu_diff.o
openzfs-zfs += $(OPENZFS)/module/zfs/dmu_direct.o
openzfs-zfs += $(OPENZFS)/module/zfs/dmu_object.o
openzfs-zfs += $(OPENZFS)/module/zfs/dmu_objset.o
openzfs-zfs += $(OPENZFS)/module/zfs/dmu_recv.o
openzfs-zfs += $(OPENZFS)/module/zfs/dmu_redact.o
openzfs-zfs += $(OPENZFS)/module/zfs/dmu_send.o
openzfs-zfs += $(OPENZFS)/module/zfs/dmu_traverse.o
openzfs-zfs += $(OPENZFS)/module/zfs/dmu_tx.o
openzfs-zfs += $(OPENZFS)/module/zfs/dmu_zfetch.o
openzfs-zfs += $(OPENZFS)/module/zfs/dnode.o
openzfs-zfs += $(OPENZFS)/module/zfs/dnode_sync.o
openzfs-zfs += $(OPENZFS)/module/zfs/dsl_bookmark.o
openzfs-zfs += $(OPENZFS)/module/zfs/dsl_crypt.o
openzfs-zfs += $(OPENZFS)/module/zfs/dsl_dataset.o
openzfs-zfs += $(OPENZFS)/module/zfs/dsl_deadlist.o
openzfs-zfs += $(OPENZFS)/module/zfs/dsl_deleg.o
openzfs-zfs += $(OPENZFS)/module/zfs/dsl_destroy.o
openzfs-zfs += $(OPENZFS)/module/zfs/dsl_dir.o
openzfs-zfs += $(OPENZFS)/module/zfs/dsl_pool.o
openzfs-zfs += $(OPENZFS)/module/zfs/dsl_prop.o
openzfs-zfs += $(OPENZFS)/module/zfs/dsl_scan.o
openzfs-zfs += $(OPENZFS)/module/zfs/dsl_synctask.o
openzfs-zfs += $(OPENZFS)/module/zfs/dsl_userhold.o
openzfs-zfs += $(OPENZFS)/module/zfs/edonr_zfs.o
openzfs-zfs += $(OPENZFS)/module/zfs/fm.o
openzfs-zfs += $(OPENZFS)/module/zfs/gzip.o
openzfs-zfs += $(OPENZFS)/module/zfs/hkdf.o
openzfs-zfs += $(OPENZFS)/module/zfs/lz4.o
openzfs-zfs += $(OPENZFS)/module/zfs/lz4_zfs.o
openzfs-zfs += $(OPENZFS)/module/zfs/lzjb.o
openzfs-zfs += $(OPENZFS)/module/zfs/metaslab.o
openzfs-zfs += $(OPENZFS)/module/zfs/mmp.o
openzfs-zfs += $(OPENZFS)/module/zfs/multilist.o
openzfs-zfs += $(OPENZFS)/module/zfs/objlist.o
openzfs-zfs += $(OPENZFS)/module/zfs/pathname.o
openzfs-zfs += $(OPENZFS)/module/zfs/range_tree.o
openzfs-zfs += $(OPENZFS)/module/zfs/refcount.o
openzfs-zfs += $(OPENZFS)/module/zfs/rrwlock.o
openzfs-zfs += $(OPENZFS)/module/zfs/sa.o
openzfs-zfs += $(OPENZFS)/module/zfs/sha2_zfs.o
openzfs-zfs += $(OPENZFS)/module/zfs/skein_zfs.o
openzfs-zfs += $(OPENZFS)/module/zfs/spa.o
openzfs-zfs += $(OPENZFS)/module/zfs/spa_checkpoint.o
openzfs-zfs += $(OPENZFS)/module/zfs/spa_config.o
openzfs-zfs += $(OPENZFS)/module/zfs/spa_errlog.o
openzfs-zfs += $(OPENZFS)/module/zfs/spa_history.o
openzfs-zfs += $(OPENZFS)/module/zfs/spa_log_spacemap.o
openzfs-zfs += $(OPENZFS)/module/zfs/spa_misc.o
openzfs-zfs += $(OPENZFS)/module/zfs/spa_stats.o
openzfs-zfs += $(OPENZFS)/module/zfs/space_map.o
openzfs-zfs += $(OPENZFS)/module/zfs/space_reftree.o
openzfs-zfs += $(OPENZFS)/module/zfs/txg.o
openzfs-zfs += $(OPENZFS)/module/zfs/uberblock.o
openzfs-zfs += $(OPENZFS)/module/zfs/unique.o
openzfs-zfs += $(OPENZFS)/module/zfs/vdev.o
openzfs-zfs += $(OPENZFS)/module/zfs/vdev_draid.o
openzfs-zfs += $(OPENZFS)/module/zfs/vdev_draid_rand.o
openzfs-zfs += $(OPENZFS)/module/zfs/vdev_file.o
openzfs-zfs += $(OPENZFS)/module/zfs/vdev_indirect.o
openzfs-zfs += $(OPENZFS)/module/zfs/vdev_indirect_births.o
openzfs-zfs += $(OPENZFS)/module/zfs/vdev_indirect_mapping.o
openzfs-zfs += $(OPENZFS)/module/zfs/vdev_initialize.o
openzfs-zfs += $(OPENZFS)/module/zfs/vdev_label.o
openzfs-zfs += $(OPENZFS)/module/zfs/vdev_mirror.o
openzfs-zfs += $(OPENZFS)/module/zfs/vdev_missing.o
openzfs-zfs += $(OPENZFS)/module/zfs/vdev_queue.o
openzfs-zfs += $(OPENZFS)/module/zfs/vdev_raidz.o
openzfs-zfs += $(OPENZFS)/module/zfs/vdev_raidz_math.o
openzfs-zfs += $(OPENZFS)/module/zfs/vdev_raidz_math_scalar.o
# x86_64 SIMD implementations
openzfs-zfs += $(OPENZFS)/module/zfs/vdev_raidz_math_avx2.o
openzfs-zfs += $(OPENZFS)/module/zfs/vdev_raidz_math_avx512bw.o
openzfs-zfs += $(OPENZFS)/module/zfs/vdev_raidz_math_avx512f.o
openzfs-zfs += $(OPENZFS)/module/zfs/vdev_raidz_math_sse2.o
openzfs-zfs += $(OPENZFS)/module/zfs/vdev_raidz_math_ssse3.o
# aarch64/powerpc (compile to empty on x64 due to #ifdef guards)
openzfs-zfs += $(OPENZFS)/module/zfs/vdev_raidz_math_aarch64_neon.o
openzfs-zfs += $(OPENZFS)/module/zfs/vdev_raidz_math_aarch64_neonx2.o
openzfs-zfs += $(OPENZFS)/module/zfs/vdev_raidz_math_powerpc_altivec.o
openzfs-zfs += $(OPENZFS)/module/zfs/vdev_rebuild.o
openzfs-zfs += $(OPENZFS)/module/zfs/vdev_removal.o
openzfs-zfs += $(OPENZFS)/module/zfs/vdev_root.o
openzfs-zfs += $(OPENZFS)/module/zfs/vdev_trim.o
openzfs-zfs += $(OPENZFS)/module/zfs/zap.o
openzfs-zfs += $(OPENZFS)/module/zfs/zap_leaf.o
openzfs-zfs += $(OPENZFS)/module/zfs/zap_micro.o
# ZFS Channel Programs (ZCP) - depends on Lua
openzfs-zfs += $(OPENZFS)/module/zfs/zcp.o
openzfs-zfs += $(OPENZFS)/module/zfs/zcp_get.o
openzfs-zfs += $(OPENZFS)/module/zfs/zcp_global.o
openzfs-zfs += $(OPENZFS)/module/zfs/zcp_iter.o
openzfs-zfs += $(OPENZFS)/module/zfs/zcp_set.o
openzfs-zfs += $(OPENZFS)/module/zfs/zcp_synctask.o
openzfs-zfs += $(OPENZFS)/module/zfs/zfeature.o
openzfs-zfs += $(OPENZFS)/module/zfs/zfs_byteswap.o
openzfs-zfs += $(OPENZFS)/module/zfs/zfs_crrd.o
openzfs-zfs += $(OPENZFS)/module/zfs/zfs_chksum.o
openzfs-zfs += $(OPENZFS)/module/zfs/zfs_debug_common.o
openzfs-zfs += $(OPENZFS)/module/zfs/zfs_fm.o
openzfs-zfs += $(OPENZFS)/module/zfs/zfs_fuid.o
openzfs-zfs += $(OPENZFS)/module/zfs/zfs_impl.o
openzfs-zfs += $(OPENZFS)/module/zfs/zfs_ioctl.o
openzfs-zfs += $(OPENZFS)/module/zfs/zfs_log.o
openzfs-zfs += $(OPENZFS)/module/zfs/zfs_onexit.o
openzfs-zfs += $(OPENZFS)/module/zfs/zfs_quota.o
openzfs-zfs += $(OPENZFS)/module/zfs/zfs_ratelimit.o
openzfs-zfs += $(OPENZFS)/module/zfs/zfs_replay.o
openzfs-zfs += $(OPENZFS)/module/zfs/zfs_rlock.o
openzfs-zfs += $(OPENZFS)/module/zfs/zfs_sa.o
openzfs-zfs += $(OPENZFS)/module/zfs/zfs_vnops.o
openzfs-zfs += $(OPENZFS)/module/zfs/zfs_znode.o
openzfs-zfs += $(OPENZFS)/module/zfs/zil.o
openzfs-zfs += $(OPENZFS)/module/zfs/zio.o
openzfs-zfs += $(OPENZFS)/module/zfs/zio_checksum.o
openzfs-zfs += $(OPENZFS)/module/zfs/zio_compress.o
openzfs-zfs += $(OPENZFS)/module/zfs/zio_inject.o
openzfs-zfs += $(OPENZFS)/module/zfs/zle.o
openzfs-zfs += $(OPENZFS)/module/zfs/zrlock.o
openzfs-zfs += $(OPENZFS)/module/zfs/zthr.o
openzfs-zfs += $(OPENZFS)/module/zfs/zvol.o

# ============================================================
# Common ZFS property/utility code (from module/zcommon/)
# ============================================================
openzfs-zcommon :=
openzfs-zcommon += $(OPENZFS)/module/zcommon/cityhash.o
openzfs-zcommon += $(OPENZFS)/module/zcommon/simd_stat.o
openzfs-zcommon += $(OPENZFS)/module/zcommon/zfeature_common.o
openzfs-zcommon += $(OPENZFS)/module/zcommon/zfs_comutil.o
openzfs-zcommon += $(OPENZFS)/module/zcommon/zfs_deleg.o
openzfs-zcommon += $(OPENZFS)/module/zcommon/zfs_fletcher.o
openzfs-zcommon += $(OPENZFS)/module/zcommon/zfs_fletcher_avx512.o
openzfs-zcommon += $(OPENZFS)/module/zcommon/zfs_fletcher_intel.o
openzfs-zcommon += $(OPENZFS)/module/zcommon/zfs_fletcher_sse.o
openzfs-zcommon += $(OPENZFS)/module/zcommon/zfs_fletcher_superscalar.o
openzfs-zcommon += $(OPENZFS)/module/zcommon/zfs_fletcher_superscalar4.o
# aarch64 fletcher (compiles to empty on x64)
openzfs-zcommon += $(OPENZFS)/module/zcommon/zfs_fletcher_aarch64_neon.o
openzfs-zcommon += $(OPENZFS)/module/zcommon/zfs_namecheck.o
openzfs-zcommon += $(OPENZFS)/module/zcommon/zfs_prop.o
openzfs-zcommon += $(OPENZFS)/module/zcommon/zfs_valstr.o
openzfs-zcommon += $(OPENZFS)/module/zcommon/zpool_prop.o
openzfs-zcommon += $(OPENZFS)/module/zcommon/zprop_common.o

# ============================================================
# AVL tree implementation (from module/avl/)
# ============================================================
openzfs-avl :=
openzfs-avl += $(OPENZFS)/module/avl/avl.o

# ============================================================
# NV pair implementation (from module/nvpair/)
# ============================================================
openzfs-nvpair :=
openzfs-nvpair += $(OPENZFS)/module/nvpair/fnvpair.o
openzfs-nvpair += $(OPENZFS)/module/nvpair/nvpair.o
openzfs-nvpair += $(OPENZFS)/module/nvpair/nvpair_alloc_fixed.o
openzfs-nvpair += $(OPENZFS)/module/nvpair/nvpair_alloc_spl.o

# ============================================================
# Unicode support (from module/unicode/)
# ============================================================
openzfs-unicode :=
openzfs-unicode += $(OPENZFS)/module/unicode/u8_textprep.o

# ============================================================
# Lua interpreter for ZFS Channel Programs (from module/lua/)
# ============================================================
openzfs-lua :=
openzfs-lua += $(OPENZFS)/module/lua/lapi.o
openzfs-lua += $(OPENZFS)/module/lua/lauxlib.o
openzfs-lua += $(OPENZFS)/module/lua/lbaselib.o
openzfs-lua += $(OPENZFS)/module/lua/lcode.o
openzfs-lua += $(OPENZFS)/module/lua/lcompat.o
openzfs-lua += $(OPENZFS)/module/lua/lcorolib.o
openzfs-lua += $(OPENZFS)/module/lua/lctype.o
openzfs-lua += $(OPENZFS)/module/lua/ldebug.o
openzfs-lua += $(OPENZFS)/module/lua/ldo.o
openzfs-lua += $(OPENZFS)/module/lua/lfunc.o
openzfs-lua += $(OPENZFS)/module/lua/lgc.o
openzfs-lua += $(OPENZFS)/module/lua/llex.o
openzfs-lua += $(OPENZFS)/module/lua/lmem.o
openzfs-lua += $(OPENZFS)/module/lua/lobject.o
openzfs-lua += $(OPENZFS)/module/lua/lopcodes.o
openzfs-lua += $(OPENZFS)/module/lua/lparser.o
openzfs-lua += $(OPENZFS)/module/lua/lstate.o
openzfs-lua += $(OPENZFS)/module/lua/lstring.o
openzfs-lua += $(OPENZFS)/module/lua/lstrlib.o
openzfs-lua += $(OPENZFS)/module/lua/ltable.o
openzfs-lua += $(OPENZFS)/module/lua/ltablib.o
openzfs-lua += $(OPENZFS)/module/lua/ltm.o
openzfs-lua += $(OPENZFS)/module/lua/lvm.o
openzfs-lua += $(OPENZFS)/module/lua/lzio.o

# ============================================================
# ICP - Illumos Crypto Port (from module/icp/)
# Required for ZFS encryption and checksumming.
# ============================================================
openzfs-icp :=
openzfs-icp += $(OPENZFS)/module/icp/illumos-crypto.o
openzfs-icp += $(OPENZFS)/module/icp/api/kcf_cipher.o
openzfs-icp += $(OPENZFS)/module/icp/api/kcf_ctxops.o
openzfs-icp += $(OPENZFS)/module/icp/api/kcf_mac.o
openzfs-icp += $(OPENZFS)/module/icp/core/kcf_callprov.o
openzfs-icp += $(OPENZFS)/module/icp/core/kcf_mech_tabs.o
openzfs-icp += $(OPENZFS)/module/icp/core/kcf_prov_lib.o
openzfs-icp += $(OPENZFS)/module/icp/core/kcf_prov_tabs.o
openzfs-icp += $(OPENZFS)/module/icp/core/kcf_sched.o
openzfs-icp += $(OPENZFS)/module/icp/spi/kcf_spi.o
openzfs-icp += $(OPENZFS)/module/icp/io/aes.o
openzfs-icp += $(OPENZFS)/module/icp/io/sha2_mod.o
openzfs-icp += $(OPENZFS)/module/icp/algs/aes/aes_impl.o
openzfs-icp += $(OPENZFS)/module/icp/algs/aes/aes_impl_aesni.o
openzfs-icp += $(OPENZFS)/module/icp/algs/aes/aes_impl_generic.o
openzfs-icp += $(OPENZFS)/module/icp/algs/aes/aes_impl_x86-64.o
openzfs-icp += $(OPENZFS)/module/icp/algs/aes/aes_modes.o
openzfs-icp += $(OPENZFS)/module/icp/algs/blake3/blake3.o
openzfs-icp += $(OPENZFS)/module/icp/algs/blake3/blake3_generic.o
openzfs-icp += $(OPENZFS)/module/icp/algs/blake3/blake3_impl.o
openzfs-icp += $(OPENZFS)/module/icp/algs/edonr/edonr.o
openzfs-icp += $(OPENZFS)/module/icp/algs/modes/ccm.o
openzfs-icp += $(OPENZFS)/module/icp/algs/modes/gcm.o
openzfs-icp += $(OPENZFS)/module/icp/algs/modes/gcm_generic.o
openzfs-icp += $(OPENZFS)/module/icp/algs/modes/gcm_pclmulqdq.o
openzfs-icp += $(OPENZFS)/module/icp/algs/modes/modes.o
openzfs-icp += $(OPENZFS)/module/icp/algs/sha2/sha256_impl.o
openzfs-icp += $(OPENZFS)/module/icp/algs/sha2/sha2_generic.o
openzfs-icp += $(OPENZFS)/module/icp/algs/sha2/sha512_impl.o
openzfs-icp += $(OPENZFS)/module/icp/algs/skein/skein.o
openzfs-icp += $(OPENZFS)/module/icp/algs/skein/skein_block.o
openzfs-icp += $(OPENZFS)/module/icp/algs/skein/skein_iv.o
openzfs-icp += $(OPENZFS)/module/icp/asm-x86_64/aes/aeskey.o
# Note: generic_impl.c is a template included by other .c files, not compiled directly

# ============================================================
# ICP Assembly routines (x86_64 SIMD crypto implementations)
# ============================================================
openzfs-icp-asm :=
openzfs-icp-asm += $(OPENZFS)/module/icp/asm-x86_64/aes/aes_amd64.o
openzfs-icp-asm += $(OPENZFS)/module/icp/asm-x86_64/aes/aes_aesni.o
# openzfs-icp-asm += $(OPENZFS)/module/icp/asm-x86_64/modes/gcm_pclmulqdq.o
# openzfs-icp-asm += $(OPENZFS)/module/icp/asm-x86_64/modes/aesni-gcm-x86_64.o
# openzfs-icp-asm += $(OPENZFS)/module/icp/asm-x86_64/modes/ghash-x86_64.o
openzfs-icp-asm += $(OPENZFS)/module/icp/asm-x86_64/sha2/sha256-x86_64.o
openzfs-icp-asm += $(OPENZFS)/module/icp/asm-x86_64/sha2/sha512-x86_64.o

# ============================================================
# ZSTD compression module (from module/zstd/)
# ============================================================
openzfs-zstd :=
openzfs-zstd += $(OPENZFS)/module/zstd/zfs_zstd.o
openzfs-zstd += $(OPENZFS)/module/zstd/zstd-in.o

# ============================================================
# ZIO crypto
#
# The core ZFS encryption implementation lives in zio_crypt_impl.c, which
# is an OSv-adapted copy of module/os/linux/zfs/zio_crypt.c compiled as
# part of openzfs-osv (see below).  It uses the ICP (module/icp/) for
# AES-256-GCM and SHA-512 HMAC — the same crypto provider already compiled
# for ZFS checksumming.  There is no separate platform-independent
# module/zfs/zio_crypt.c to compile: zio_crypt_impl.c covers both the
# OS-specific wrappers and the algorithm logic in one file.
#
# module/os/osv/zfs/zio_crypt_os.c contains superseded ENOTSUP stubs kept
# only as documentation; it must NOT be added here.
# ============================================================
openzfs-crypt :=

# ============================================================
# OSv-specific ZFS code (from module/os/osv/zfs/)
# ============================================================
openzfs-osv :=
openzfs-osv += $(OPENZFS)/module/os/osv/zfs/abd_os.o
openzfs-osv += $(OPENZFS)/module/os/osv/zfs/arc_os.o
openzfs-osv += $(OPENZFS)/module/os/osv/zfs/dmu_os.o
openzfs-osv += $(OPENZFS)/module/os/osv/zfs/event_os.o
openzfs-osv += $(OPENZFS)/module/os/osv/zfs/kmod_core.o
openzfs-osv += $(OPENZFS)/module/os/osv/zfs/spa_os.o
openzfs-osv += $(OPENZFS)/module/os/osv/zfs/spl_uio.o
openzfs-osv += $(OPENZFS)/module/os/osv/zfs/sysctl_os.o
openzfs-osv += $(OPENZFS)/module/os/osv/zfs/vdev_disk.o
openzfs-osv += $(OPENZFS)/module/os/osv/zfs/vdev_label_os.o
openzfs-osv += $(OPENZFS)/module/os/osv/zfs/zfs_acl.o
openzfs-osv += $(OPENZFS)/module/os/osv/zfs/zfs_ctldir.o
openzfs-osv += $(OPENZFS)/module/os/osv/zfs/zfs_debug.o
openzfs-osv += $(OPENZFS)/module/os/osv/zfs/zfs_dir.o
openzfs-osv += $(OPENZFS)/module/os/osv/zfs/zfs_file_os.o
openzfs-osv += $(OPENZFS)/module/os/osv/zfs/zfs_initialize_osv.o
openzfs-osv += $(OPENZFS)/module/os/osv/zfs/zfs_ioctl_os.o
openzfs-osv += $(OPENZFS)/module/os/osv/zfs/zfs_racct.o
openzfs-osv += $(OPENZFS)/module/os/osv/zfs/zfs_vfsops.o
openzfs-osv += $(OPENZFS)/module/os/osv/zfs/zfs_vnops_os.o
openzfs-osv += $(OPENZFS)/module/os/osv/zfs/zfs_znode_os.o
openzfs-osv += $(OPENZFS)/module/os/osv/zfs/zfs_auto_upgrade.o
openzfs-osv += $(OPENZFS)/module/os/osv/zfs/zvol_os.o
openzfs-osv += $(OPENZFS)/module/os/osv/zfs/zio_crypt_impl.o
# NOTE: list.o replaces the old OpenSolaris list.o (bsd/sys/cddl/contrib/.../os/list.o).
# The old version was compiled with the 32-byte list_impl.h (list_size + list_offset +
# list_head), but OpenZFS module code uses the 24-byte list_impl.h (list_offset +
# list_head only).  Compiling list.o with OPENZFS_CFLAGS ensures both use the same
# struct layout, preventing NULL-pointer crashes in list_insert_tail during vdev I/O.
openzfs-osv += $(OPENZFS)/module/os/freebsd/spl/list.o

# ============================================================
# Compatibility shim (OSv compat layer glue)
# ============================================================
openzfs-compat :=
openzfs-compat += bsd/sys/cddl/compat/opensolaris/openzfs_osv_compat.o

# ============================================================
# Combined OpenZFS object list (replaces old 'zfs' variable)
# ============================================================
openzfs-all := $(openzfs-zfs) $(openzfs-zcommon) $(openzfs-avl) \
	$(openzfs-nvpair) $(openzfs-unicode) $(openzfs-lua) \
	$(openzfs-icp) $(openzfs-icp-asm) $(openzfs-zstd) \
	$(openzfs-crypt) $(openzfs-osv) $(openzfs-compat)

# ============================================================
# Include paths for OpenZFS
# ============================================================
OPENZFS_INCLUDES := \
	-I$(OPENZFS)/include \
	-I$(OPENZFS)/include/os/osv/spl \
	-I$(OPENZFS)/include/os/osv/zfs \
	-I$(OPENZFS)/module/icp/include \
	-Ibsd/sys/cddl/compat/opensolaris \
	-Ibsd/sys/cddl/contrib/opensolaris/uts/common \
	-Ibsd/sys \
	-Ibsd/porting \
	-Iinclude \
	-I.

# ============================================================
# CFLAGS for OpenZFS compilation
# ============================================================
OPENZFS_CFLAGS := \
	$(OPENZFS_INCLUDES) \
	-D__KERNEL__ \
	-D_KERNEL \
	-D__OSV__ \
	-U__linux__ \
	-DNDEBUG \
	-DHAVE_ISSETUGID \
	-DTEXT_DOMAIN=\"zfs-osv\" \
	-Wno-unused-function \
	-Wno-pointer-sign \
	-Wno-incompatible-pointer-types \
	-Dcv_timedwait=openzfs_cv_timedwait \
	-include $(OPENZFS)/include/os/osv/zfs/sys/zfs_context_os.h

# Lua files need setjmp.h and must #undef panic (conflict with struct member)
OPENZFS_LUA_CFLAGS := \
	-include $(OPENZFS)/include/os/osv/zfs/sys/zfs_lua_fix.h \
	-Wno-infinite-recursion
