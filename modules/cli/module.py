from osv.modules.api import *
from osv.modules.filemap import FileMap
from osv.modules import api
import os, os.path

require('lua')
require('ncurses')
require('libedit')

usr_files = FileMap()
usr_files.add('${OSV_BASE}/modules/cli').to('/cli') \
	.include('cli.so') \
	.include('cli.lua') \
	.include('lib/**') \
	.include('commands/**')

_httpserver_module = require('httpserver')
httpserver = _httpserver_module.default

full = [
    api.run('/cli/cli.so'),
    httpserver,
]

default = full
