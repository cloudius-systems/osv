from osv.modules import api
import os

_modules_base = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
_java_test_commands_file = _modules_base + '/java-tests/test_commands'

host_arch = os.uname().machine
if host_arch == 'x86_64' and os.getenv('ARCH') == 'aarch64':
    if os.path.exists(_java_test_commands_file):
        os.remove(_java_test_commands_file)
else:
    api.require('java-tests')

api.require('dl_tests')
