from osv.modules import api
from osv.modules.filemap import FileMap
import subprocess

api.require('java')

javac_with_version = subprocess.check_output(['javac', '-version'], stderr=subprocess.STDOUT).decode('utf-8')

usr_files = FileMap()
_jar = '/tests/java/tests.jar'

if javac_with_version.startswith('javac 1.8'):
    _isolates_jar = '/tests/java/isolates.jar'

    usr_files.add('${OSV_BASE}/modules/java-tests/tests/target/runjava-tests.jar').to(_jar)
    usr_files.add('${OSV_BASE}/modules/java-tests/tests-isolates/target/tests-isolates-jar-with-dependencies.jar').to(_isolates_jar)

    usr_files.add('${OSV_BASE}/modules/java-tests/tests/target/classes/tests/ClassPutInRoot.class').to('/tests/ClassPutInRoot.class')

    usr_files.add('${OSV_BASE}/modules/java-tests/tests-jre-extension/target/tests-jre-extension.jar') \
        .to('/usr/lib/jvm/java/jre/lib/ext/tests-jre-extension.jar')

    usr_files.add('${OSV_BASE}/build/last/modules/java-tests/java_isolated.so').to('/java_isolated.so')
    usr_files.add('${OSV_BASE}/modules/java-base/runjava-isolated/target/runjava-isolated.jar').to('/java/runjava-isolated.jar')
    usr_files.add('${OSV_BASE}/modules/java-tests/.java.policy').to('/.java.policy')
else:
    usr_files.add('${OSV_BASE}/modules/java-tests/tests-for-java9_1x/target/runjava-9-1x-tests.jar').to(_jar)
