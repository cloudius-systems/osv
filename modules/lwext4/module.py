import os
from osv.modules.filemap import FileMap

_module = '${OSV_BASE}/modules/lwext4'

usr_files = FileMap()
if os.environ.get("fs_type") != 'ext':
    usr_files.add(os.path.join(_module, 'upstream/lwext4/build_lib_only/src/liblwext4.so')).to('/usr/lib/liblwext4.so')
