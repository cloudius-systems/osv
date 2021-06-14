from osv.modules import api
import subprocess

javac_with_version = subprocess.check_output(['javac', '-version'], stderr=subprocess.STDOUT).decode('utf-8')

if javac_with_version.startswith('javac 1.8'):
    api.require('java-non-isolated')
    api.require('openjdk8-from-host')
else:
    api.require('openjdk9_1x-from-host')
