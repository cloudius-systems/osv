from osv.modules import api
from osv.modules.filemap import FileMap

api.require('java')

_jar = '/tests/java/tests.jar'
_isolates_jar = '/tests/java/isolates.jar'

usr_files = FileMap()
usr_files.add('${OSV_BASE}/modules/java-tests/tests/target/runjava-tests.jar').to(_jar)
usr_files.add('${OSV_BASE}/modules/java-tests/tests-isolates/target/tests-isolates-jar-with-dependencies.jar').to(_isolates_jar)

usr_files.add('${OSV_BASE}/modules/java-tests/tests/target/classes/tests/ClassPutInRoot.class').to('/tests/ClassPutInRoot.class')

usr_files.add('${OSV_BASE}/modules/java-tests/tests-jre-extension/target/tests-jre-extension.jar') \
    .to('/usr/lib/jvm/java/jre/lib/ext/tests-jre-extension.jar')
