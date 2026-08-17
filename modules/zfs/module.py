import os
from osv.modules import api

# Placeholder `zfs` module: selects the concrete in-kernel ZFS implementation
# based on conf_zfs, analogous to how the `java` placeholder module selects a
# concrete JDK provider.  conf_zfs=openzfs -> `open_zfs`, otherwise -> `bsd_zfs`;
# each of those `provides = ['zfs']`.  The libsolaris.so manifest entry that
# every ZFS image needs lives in this module's usr.manifest (shared by both
# implementations).
if os.environ.get('conf_zfs', 'bsd') == 'openzfs':
    api.require('open_zfs')
else:
    api.require('bsd_zfs')
