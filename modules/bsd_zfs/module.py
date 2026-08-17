from osv.modules import api

# The `bsd_zfs` module PROVIDES the `zfs` capability for the legacy in-tree
# BSD/Illumos ZFS implementation (conf_zfs=bsd), analogous to how
# `openjdk8-from-host` provides `java`.  The actual kernel objects that make up
# BSD ZFS are linked into libsolaris.so by the top-level Makefile, which
# includes modules/bsd_zfs/bsd_zfs_sources.mk.  This module carries no extra
# manifest of its own; the `zfs` placeholder module selects it via conf_zfs.
provides = ['zfs']
