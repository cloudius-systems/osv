import os
from osv.modules.api import *
from osv.modules.filemap import FileMap
from osv.modules import api

_module = '${OSV_BASE}/modules/monitoring-agent'

usr_files = FileMap()
usr_files.add(os.path.join(_module, 'monitoring-agent.so')).to('/usr/mgmt/monitoring-agent.so')
usr_files.add(os.path.join(_module, 'cmdline')).to('/init/01-cmdline')

default = ""
