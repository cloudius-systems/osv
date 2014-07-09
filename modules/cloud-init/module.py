import os
from osv.modules.api import *
from osv.modules.filemap import FileMap
from osv.modules import api

_module = '${OSV_BASE}/modules/cloud-init'

usr_files = FileMap()
usr_files.add(os.path.join('${OSV_BASE}', 'external/x64/misc.bin/usr/lib64/libyaml-cpp.so.0.5.1')).to('/usr/lib/libyaml-cpp.so.0.5')
usr_files.add(os.path.join(_module, 'cloud-init.so')).to('/usr/mgmt/cloud-init.so')
usr_files.add(os.path.join(_module, 'cloud-init.yaml')).to('/usr/mgmt/cloud-init.yaml')

api.require('httpserver')

#default = api.run('/usr/mgmt/cloud-init.so --skip-error --file /usr/mgmt/cloud-init.yaml')
default = ""
