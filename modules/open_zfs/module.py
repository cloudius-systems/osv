from osv.modules import api
import os

# The `open_zfs` module PROVIDES the `zfs` capability for the vendored OpenZFS
# 2.4.x implementation (conf_zfs=openzfs), analogous to how
# `openjdk9_1x-from-host` provides `java`.  The OpenZFS submodule lives in
# modules/open_zfs/openzfs (pristine upstream), the OSv platform layer as
# normal parent-tracked source in modules/open_zfs/osv, and a single small patch
# for the ~15 edited upstream files in modules/open_zfs/patches (applied at build
# time).  The kernel objects are linked into libsolaris.so by the top-level Makefile, which for
# conf_zfs=openzfs includes modules/open_zfs/open_zfs_sources.mk.
#
# libsolaris.so must be listed by the module that actually provides ZFS, not by
# the `zfs` placeholder: the placeholder is replaced by its required provider
# during module resolution, so a manifest carried only by the placeholder is
# dropped and libsolaris.so never lands in usr.manifest.  That silently omits
# it from a bootfs-populated (fs=ramfs) image, where --preload-zfs-library then
# fails to dlopen /usr/lib/fs/libsolaris.so.  Generate the manifest entry here
# (written at import time, matching zfs-tools) so the provider carries it.
_manifest = os.path.join(os.path.dirname(__file__), 'usr.manifest')
with open(_manifest, 'w') as f:
    f.write('[manifest]\n')
    f.write('/usr/lib/fs/libsolaris.so: libsolaris.so\n')

provides = ['zfs']
