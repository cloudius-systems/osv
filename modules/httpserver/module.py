import os
from osv.modules.api import *
from osv.modules.filemap import FileMap
from osv.modules import api

_module = '${OSV_BASE}/modules/httpserver'

_exe = '/libhttpserver.so'

usr_files = FileMap()
usr_files.add(os.path.join(_module, 'libhttpserver.so')).to(_exe)
usr_files.add(os.path.join(_module, 'api-doc')).to('/usr/mgmt/api')
usr_files.add(os.path.join(_module, 'swagger-ui', 'dist')).to('/usr/mgmt/swagger-ui/dist')
usr_files.add(os.path.join(_module, 'osv-gui/public')).to('/usr/mgmt/gui')

api.require('openssl')
api.require('libtools')
api.require('libyaml')

# only require next 3 modules if java (jre) is included in the list of modules
api.require_if_other_module_present('josvsym','java')
api.require_if_other_module_present('httpserver-jolokia-plugin','java')
api.require_if_other_module_present('httpserver-jvm-plugin','java')

# httpserver will run regardless of an explicit command line
# passed with "run.py -e".
daemon = api.run_on_init(_exe + ' &!')

fg = api.run(_exe)

fg_ssl = api.run(_exe + ' --ssl')

default = daemon
