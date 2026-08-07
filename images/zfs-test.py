from osv.modules.api import *

# ZFS kernel module (libsolaris.so)
require('zfs')

# ZFS userspace tools: zpool.so, zfs.so, libzfs.so, libuutil.so
require('zfs-tools')

# OSv test binaries (tst-zfs-direct-io.so, tst-crucible-blk.so, ...).
# Setting OSV_NO_JAVA_TESTS=1 keeps the java-tests submodule from being
# pulled in (it requires p11-kit / OpenJDK on the build host).
require('tests')

run = []
