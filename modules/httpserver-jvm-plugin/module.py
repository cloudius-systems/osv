import os
from osv.modules.api import *
from osv.modules.filemap import FileMap
from osv.modules import api

_module = '${OSV_BASE}/modules/httpserver-jvm-plugin'

usr_files = FileMap()
usr_files.add(os.path.join(_module, 'jvm.so')).to('/usr/mgmt/plugins/jvm.so')
usr_files.add(os.path.join(_module, 'api-doc/listings/jvm.json')).to('/usr/mgmt/api/listings/jvm.json')
