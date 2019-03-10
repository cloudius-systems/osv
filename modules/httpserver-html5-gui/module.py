import os
from osv.modules.api import *
from osv.modules.filemap import FileMap
from osv.modules import api

_module = '${OSV_BASE}/modules/httpserver-html5-gui'

usr_files = FileMap()
usr_files.add(os.path.join(_module, 'swagger-ui', 'dist')).to('/usr/mgmt/swagger-ui/dist')
usr_files.add(os.path.join(_module, 'osv-gui/public')).to('/usr/mgmt/gui')
usr_files.add(os.path.join(_module, 'httpserver.conf')).to('/etc/httpserver.conf')

api.require('httpserver-api')

# httpserver will run regardless of an explicit command line
# passed with "run.py -e".
_exe = '/libhttpserver-api.so --config-file=/etc/httpserver.conf'
daemon = api.run_on_init(_exe + ' &!')

fg = api.run(_exe)

fg_ssl = api.run(_exe + ' --ssl')
fg_cors = api.run(_exe + ' --access-allow=true')

default = daemon
