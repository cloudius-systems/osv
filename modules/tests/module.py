from osv.modules import api
import os

_modules_base = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
_java_test_commands_file = _modules_base + '/java-tests/test_commands'

# OSV_NO_JAVA_TESTS=1 lets a build host without OpenJDK / p11-kit still
# build the C test image (e.g. NixOS without the java-tests dependencies
# in the dev shell).  When set, we suppress the java-tests pull and the
# dl_tests pull (dl_tests links against shared-library probes that the
# java path drags in too).
_no_java = os.getenv('OSV_NO_JAVA_TESTS') == '1'

host_arch = os.uname().machine
if host_arch == 'x86_64' and os.getenv('ARCH') == 'aarch64':
    if os.path.exists(_java_test_commands_file):
        os.remove(_java_test_commands_file)
elif not _no_java:
    api.require('java-tests')

if not _no_java:
    api.require('dl_tests')
