import os
from osv.modules import api
from osv.modules.filemap import FileMap

build_dir = 'build/' + os.environ['mode'] + '.' + os.environ['ARCH']

usr_files = FileMap()
if os.environ.get("fs_type") != 'ext':
    usr_files.add('${OSV_BASE}/' + build_dir + '/modules/libext/libext.so').to('/usr/lib/fs/libext.so')

api.require('lwext4')
