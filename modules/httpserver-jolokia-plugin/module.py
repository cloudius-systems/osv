import os
from osv.modules.api import *
from osv.modules.filemap import FileMap
from osv.modules import api

_module = '${OSV_BASE}/modules/httpserver-jolokia-plugin'

usr_files = FileMap()
usr_files.add(os.path.join(_module, 'api-doc/listings/jolokia.json')).to('/usr/mgmt/api/listings/jolokia.json')
usr_files.add(os.path.join(_module, 'jolokia-agent/target/jolokia-agent.jar')).to('/usr/mgmt/jolokia-agent.jar')
