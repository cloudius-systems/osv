import os
from osv.modules.api import *
from osv.modules.filemap import FileMap
from osv.modules import api

provides = ['httpserver-api']

_module = '${OSV_BASE}/modules/httpserver-monitoring-api'

_exe = '/libhttpserver-api.so'

usr_files = FileMap()
usr_files.link(_exe).to('/usr/lib/libhttpserver-api.so')
usr_files.add(os.path.join(_module, 'api-doc')).to('/usr/mgmt/api')

api.require('libtools')

# httpserver will run regardless of an explicit command line
# passed with "run.py -e".
daemon = api.run_on_init(_exe + ' --access-allow=true &!')

fg = api.run(_exe + ' --access-allow=true')

default = daemon
