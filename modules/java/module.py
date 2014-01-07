from osv.modules.filemap import FileMap

usr_files = FileMap()

usr_files.add('${jdkbase}').to('/usr/lib/jvm') \
    .include('lib/**') \
    .include('jre/**') \
    .exclude('jre/lib/security/cacerts') \
    .exclude('jre/lib/audio/**')
