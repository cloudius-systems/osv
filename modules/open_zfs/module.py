from osv.modules import api

# The `open_zfs` module PROVIDES the `zfs` capability for the vendored OpenZFS
# 2.4.x implementation (conf_zfs=openzfs), analogous to how
# `openjdk9_1x-from-host` provides `java`.  The OpenZFS submodule lives in
# modules/open_zfs/openzfs and our OSv platform-layer patches in
# modules/open_zfs/patches (applied at build time).  The kernel objects are
# linked into libsolaris.so by the top-level Makefile, which for
# conf_zfs=openzfs includes modules/open_zfs/open_zfs_sources.mk.  This module
# carries no extra manifest of its own; the `zfs` placeholder module selects it
# via conf_zfs.
provides = ['zfs']
