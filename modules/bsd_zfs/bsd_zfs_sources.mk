# BSD/Illumos ZFS (c. 2014) kernel source objects for libsolaris.so.
#
# Owned by the `bsd_zfs` module (see modules/bsd_zfs/module.py, which
# `provides` the `zfs` capability for conf_zfs=bsd).  These are kernel-side
# objects linked into libsolaris.so, so the rules live in an .mk fragment the
# top-level Makefile `include`s rather than in a module app Makefile.
#
# Included unconditionally by the top-level Makefile: it defines the `solaris`
# (compat layer) and `zfs` (ZFS core) object lists that BOTH ZFS modes share,
# then applies the BSD-only object/flag selections.  For conf_zfs=openzfs the
# Makefile additionally includes modules/open_zfs/open_zfs_sources.mk, which
# swaps in $(openzfs-all) and OpenZFS flags.

solaris :=
solaris += bsd/sys/cddl/compat/opensolaris/kern/opensolaris.o
solaris += bsd/sys/cddl/compat/opensolaris/kern/opensolaris_atomic.o
solaris += bsd/sys/cddl/compat/opensolaris/kern/opensolaris_cmn_err.o
solaris += bsd/sys/cddl/compat/opensolaris/kern/opensolaris_kmem.o
solaris += bsd/sys/cddl/compat/opensolaris/kern/opensolaris_kobj.o
solaris += bsd/sys/cddl/compat/opensolaris/kern/opensolaris_kstat.o
solaris += bsd/sys/cddl/compat/opensolaris/kern/opensolaris_policy.o
solaris += bsd/sys/cddl/compat/opensolaris/kern/opensolaris_sunddi.o
solaris += bsd/sys/cddl/compat/opensolaris/kern/opensolaris_string.o
solaris += bsd/sys/cddl/compat/opensolaris/kern/opensolaris_sysevent.o
solaris += bsd/sys/cddl/compat/opensolaris/kern/opensolaris_taskq.o
solaris += bsd/sys/cddl/compat/opensolaris/kern/opensolaris_uio.o
solaris += bsd/sys/cddl/contrib/opensolaris/common/acl/acl_common.o
solaris += bsd/sys/cddl/contrib/opensolaris/uts/common/os/callb.o
solaris += bsd/sys/cddl/contrib/opensolaris/uts/common/zmod/adler32.o
solaris += bsd/sys/cddl/contrib/opensolaris/uts/common/zmod/deflate.o
solaris += bsd/sys/cddl/contrib/opensolaris/uts/common/zmod/inffast.o
solaris += bsd/sys/cddl/contrib/opensolaris/uts/common/zmod/inflate.o
solaris += bsd/sys/cddl/contrib/opensolaris/uts/common/zmod/inftrees.o
solaris += bsd/sys/cddl/contrib/opensolaris/uts/common/zmod/opensolaris_crc32.o
solaris += bsd/sys/cddl/contrib/opensolaris/uts/common/zmod/trees.o
solaris += bsd/sys/cddl/contrib/opensolaris/uts/common/zmod/zmod.o
solaris += bsd/sys/cddl/contrib/opensolaris/uts/common/zmod/zmod_subr.o
solaris += bsd/sys/cddl/contrib/opensolaris/uts/common/zmod/zutil.o

zfs += bsd/sys/cddl/contrib/opensolaris/common/zfs/zfeature_common.o
zfs += bsd/sys/cddl/contrib/opensolaris/common/zfs/zfs_comutil.o
zfs += bsd/sys/cddl/contrib/opensolaris/common/zfs/zfs_deleg.o
zfs += bsd/sys/cddl/contrib/opensolaris/common/zfs/zfs_fletcher.o
zfs += bsd/sys/cddl/contrib/opensolaris/common/zfs/zfs_ioctl_compat.o
zfs += bsd/sys/cddl/contrib/opensolaris/common/zfs/zfs_namecheck.o
zfs += bsd/sys/cddl/contrib/opensolaris/common/zfs/zfs_prop.o
zfs += bsd/sys/cddl/contrib/opensolaris/common/zfs/zpool_prop.o
zfs += bsd/sys/cddl/contrib/opensolaris/common/zfs/zprop_common.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/arc.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/bplist.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/bpobj.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/bptree.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/dbuf.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/ddt.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/ddt_zap.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/dmu.o
#zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/dmu_diff.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/dmu_object.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/dmu_objset.o
#zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/dmu_send.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/dmu_traverse.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/dmu_tx.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/dmu_zfetch.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/dnode.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/dnode_sync.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/dsl_dataset.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/dsl_deadlist.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/dsl_deleg.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/dsl_dir.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/dsl_pool.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/dsl_prop.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/dsl_scan.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/dsl_synctask.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/gzip.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/lzjb.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/metaslab.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/refcount.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/rrwlock.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/sa.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/sha256.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/spa.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/space_map.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/spa_config.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/spa_errlog.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/spa_history.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/spa_misc.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/txg.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/uberblock.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/unique.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/vdev.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/vdev_cache.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/vdev_disk.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/vdev_file.o
#zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/vdev_geom.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/vdev_label.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/vdev_mirror.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/vdev_missing.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/vdev_queue.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/vdev_raidz.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/vdev_root.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zap.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zap_leaf.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zap_micro.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zfeature.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zfs_acl.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zfs_byteswap.o
#zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zfs_ctldir.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zfs_debug.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zfs_dir.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zfs_fm.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zfs_fuid.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zfs_ioctl.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zfs_init.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zfs_log.o
#zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zfs_onexit.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zfs_replay.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zfs_rlock.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zfs_sa.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zfs_vfsops.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zfs_vnops.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zfs_znode.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zil.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zio.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zio_checksum.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zio_compress.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zio_inject.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zle.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zrlock.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/zvol.o
zfs += bsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/lz4.o

# --- conf_zfs=bsd object/flag selection -------------------------------------
# For conf_zfs=openzfs the top-level Makefile skips this block and includes
# modules/open_zfs/open_zfs_sources.mk instead (which adds $(openzfs-all),
# drops the ABI-incompatible kstat stub, and sets OpenZFS CFLAGS).
ifneq ($(conf_zfs),openzfs)
# conf_zfs=bsd: legacy in-tree BSD/Illumos ZFS.
solaris += $(zfs)

# Common objects that OpenZFS otherwise provides (openzfs-avl/nvpair/unicode/
# fm/list); BSD ZFS needs its own copies.
solaris += bsd/sys/cddl/contrib/opensolaris/common/avl/avl.o
solaris += bsd/sys/cddl/contrib/opensolaris/common/nvpair/fnvpair.o
solaris += bsd/sys/cddl/contrib/opensolaris/common/nvpair/nvpair.o
$(out)/bsd/sys/cddl/contrib/opensolaris/common/nvpair/nvpair.o: CFLAGS += -Wno-stringop-overread
solaris += bsd/sys/cddl/contrib/opensolaris/common/nvpair/nvpair_alloc_fixed.o
solaris += bsd/sys/cddl/contrib/opensolaris/common/unicode/u8_textprep.o
solaris += bsd/sys/cddl/contrib/opensolaris/uts/common/os/fm.o
solaris += bsd/sys/cddl/contrib/opensolaris/uts/common/os/list.o
solaris += bsd/sys/cddl/contrib/opensolaris/uts/common/os/nvpair_alloc_system.o

$(zfs:%=$(out)/%): CFLAGS+= \
	-DBUILDING_ZFS \
	-Wno-array-bounds \
	-Ibsd/sys/cddl/contrib/opensolaris/uts/common/fs/zfs \
	-Ibsd/sys/cddl/contrib/opensolaris/common/zfs

$(solaris:%=$(out)/%): CFLAGS+= \
	-fno-strict-aliasing \
	-Wno-unknown-pragmas \
	-Wno-unused-variable \
	-Wno-switch \
	-Wno-maybe-uninitialized \
	-Ibsd/sys/cddl/compat/opensolaris \
	-Ibsd/sys/cddl/contrib/opensolaris/common \
	-Ibsd/sys/cddl/contrib/opensolaris/uts/common \
	-Ibsd/sys

$(solaris:%=$(out)/%): ASFLAGS+= \
	-Ibsd/sys/cddl/contrib/opensolaris/uts/common
endif
