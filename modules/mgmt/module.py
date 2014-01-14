from osv.modules.api import *
from osv.modules.filemap import FileMap

usr_files = FileMap()

usr_files.add('${OSV_BASE}/mgmt/httpserver/httpserver.so').to('/usr/mgmt/httpserver.so')

_web_jar = '/usr/mgmt/web-1.0.0.jar'
_logging_opts = '-Djava.util.logging.config.file=/usr/mgmt/config/logging.properties'

shell = run_java(
        jvm_args=[_logging_opts],
        classpath=[
            '/usr/mgmt/lib/bcprov-jdk15on-147.jar',
            '/usr/mgmt/lib/bcpkix-jdk15on-147.jar',
            _web_jar,
        ],
        args=[
            '-jar', '/usr/mgmt/crash-1.0.0.jar'
        ])

web = run_java(
        jvm_args=[_logging_opts],
        args=['-jar', _web_jar, 'app', 'prod'])

full = [
    shell,
    delayed(web, 3000)
]
