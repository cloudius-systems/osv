import os
from osv.modules.api import *
from osv.modules.filemap import FileMap
from osv.modules import api

_module = '${OSV_BASE}/mgmt/osvinit'

usr_files = FileMap()
usr_files.add(os.path.join('${OSV_BASE}', 'external/x64/misc.bin/usr/lib64/libyaml-cpp.so.0.5.1')).to('/usr/lib/libyaml-cpp.so.0.5')
usr_files.add(os.path.join(_module, 'osvinit.so')).to('/usr/mgmt/osvinit.so')
usr_files.add(os.path.join(_module, 'osvinit.yaml')).to('/usr/mgmt/osvinit.yaml')

api.require('httpserver')

#default = api.run('/usr/mgmt/osvinit.so --skip-error --file /usr/mgmt/osvinit.yaml')
default = ""
