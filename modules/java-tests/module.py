from osv.modules import api
from osv.modules.filemap import FileMap

api.require('java')

_jar = '/tests/java/tests.jar'
_isolates_jar = '/tests/java/isolates.jar'

usr_files = FileMap()
usr_files.add('${OSV_BASE}/java/tests/target/runjava-tests.jar').to(_jar)
usr_files.add('${OSV_BASE}/java/tests-isolates/target/tests-isolates-jar-with-dependencies.jar').to(_isolates_jar)

run_tests = api.run_java(classpath=[_jar, _isolates_jar],
    args=['-Disolates.jar=' + _isolates_jar, 'org.junit.runner.JUnitCore', 'io.osv.AllTests'])
