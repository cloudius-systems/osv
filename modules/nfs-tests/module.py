import os
from osv.modules.filemap import FileMap
from osv.modules import api

_module = '${OSV_BASE}/modules/nfs-tests'

usr_files = FileMap()
usr_files.add(os.path.join(_module, 'tst-nfs.so')).to('/tst-nfs.so')
usr_files.add(os.path.join(_module, 'fsx-linux.so')).to('/fsx-linux.so')
