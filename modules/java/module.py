from osv.modules.filemap import FileMap
from osv.modules import api

usr_files = FileMap()

api.require('fonts')

usr_files.add('${jdkbase}').to('/usr/lib/jvm') \
    .include('lib/**') \
    .include('jre/**') \
    .exclude('jre/lib/security/cacerts') \
    .exclude('jre/lib/audio/**')
