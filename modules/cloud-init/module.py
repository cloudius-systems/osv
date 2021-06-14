import os
from osv.modules.api import *
from osv.modules.filemap import FileMap
from osv.modules import api

_module = '${OSV_BASE}/modules/cloud-init'

usr_files = FileMap()
usr_files.add(os.path.join(_module, 'cloud-init.yaml')).to('/usr/mgmt/cloud-init.yaml')
usr_files.add(os.path.join(_module, 'cmdline')).to('/init/00-cmdline')

api.require('httpserver')
api.require('libyaml')
api.require('libcdio')

#default = api.run('/usr/mgmt/cloud-init.so --skip-error --file /usr/mgmt/cloud-init.yaml')
default = ""
