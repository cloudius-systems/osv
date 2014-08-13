import os
from osv.modules.api import *
from osv.modules.filemap import FileMap
from osv.modules import api

_module = '${OSV_BASE}/modules/httpserver'

usr_files = FileMap()
usr_files.add(os.path.join(_module, 'libhttpserver.so')).to('/libhttpserver.so')
usr_files.add(os.path.join(_module, 'api-doc')).to('/usr/mgmt/api')
usr_files.add(os.path.join(_module, 'swagger-ui', 'dist')).to('/usr/mgmt/swagger-ui/dist')
usr_files.add('${OSV_BASE}/java/jolokia-agent/target/jolokia-agent.jar').to('/usr/mgmt/jolokia-agent.jar')

#default = api.run('/libhttpserver.so')

# Instead of adding to the command line using api.run(...), put a command in
# /init. This way, httpserver will run regardless of an explicit command line
# passed with "run.py -e".
usr_files.add(os.path.join(_module, 'cmdline')).to('/init/10-httpserver')
api.require('libtools')
