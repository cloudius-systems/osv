from osv.modules import api
from osv.modules.filemap import FileMap
import os, os.path

api.require('java-base')

usr_files = FileMap()
jdkdir = os.path.basename(os.path.expandvars('${jdkbase}'))

usr_files.add('${jdkbase}').to('/usr/lib/jvm/java') \
    .include('lib/**') \
    .include('jre/**') \
    .exclude('jre/lib/security/cacerts') \
    .exclude('jre/lib/audio/**')

usr_files.link('/usr/lib/jvm/' + jdkdir).to('java')
usr_files.link('/usr/lib/jvm/jre').to('java/jre')
usr_files.link('/usr/lib/jvm/java/jre/lib/security/cacerts').to('/etc/pki/java/cacerts')

_jar = '/tests/java/tests.jar'
_isolates_jar = '/tests/java/isolates.jar'

usr_files.add('${OSV_BASE}/modules/java-tests/tests/target/runjava-tests.jar').to(_jar)
usr_files.add('${OSV_BASE}/modules/java-tests/tests-isolates/target/tests-isolates-jar-with-dependencies.jar').to(_isolates_jar)

usr_files.add('${OSV_BASE}/modules/java-tests/tests/target/classes/tests/ClassPutInRoot.class').to('/tests/ClassPutInRoot.class')

usr_files.add('${OSV_BASE}/modules/java-tests/tests-jre-extension/target/tests-jre-extension.jar') \
    .to('/usr/lib/jvm/java/jre/lib/ext/tests-jre-extension.jar')
