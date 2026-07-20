import os

# ZFS userspace tools manifest.  Generated at import time (plain text, not a
# FileMap) because FileMap eagerly validates host paths that the build has not
# produced yet.  The set of libraries depends on the selected ZFS impl:
#   conf_zfs=openzfs also ships libzfs_core/libzutil/libshare/libtpool, which
#   the legacy BSD ZFS userspace does not build.
_libs = ['zpool.so', 'zfs.so', 'libzfs.so', 'libuutil.so']
if os.environ.get('conf_zfs', 'bsd') == 'openzfs':
    _libs += ['libzfs_core.so', 'libzutil.so', 'libshare.so', 'libtpool.so']

_manifest = os.path.join(os.path.dirname(__file__), 'usr.manifest')
with open(_manifest, 'w') as f:
    f.write('[manifest]\n')
    for lib in _libs:
        f.write('/%s: %s\n' % (lib, lib))
