from osv.modules import api
import os
from distutils.spawn import find_executable

_modules_base = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
_java_test_commands_file = _modules_base + '/java-tests/test_commands'

host_arch = os.uname().machine
if (host_arch == 'x86_64' and os.getenv('ARCH') == 'aarch64') or \
   os.getenv('OSV_NO_JAVA_TESTS') == '1' or not find_executable('javac'):
    # No javac available (or explicitly disabled): skip the Java test module so
    # the C/C++ test image still builds.
    if os.path.exists(_java_test_commands_file):
        os.remove(_java_test_commands_file)
else:
    api.require('java-tests')

api.require('dl_tests')
