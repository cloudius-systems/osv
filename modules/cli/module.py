from osv.modules.api import *
from osv.modules.filemap import FileMap
from osv.modules import api

require('lua')
require('terminfo')
require_running('httpserver')

usr_files = FileMap()
usr_files.add('${OSV_BASE}/modules/cli').to('/cli-app') \
	.include('cli.lua') \
	.include('lib/**') \
	.include('commands/**')

full = api.run('/cli')
default = full
