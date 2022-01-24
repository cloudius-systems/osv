from osv.modules.filemap import FileMap
from osv.modules import api
import os, os.path
import subprocess

#Verify that the jdk exists by trying to locate javac (java compiler)
if subprocess.call(['which', 'javac']) != 0:
    print('Could not find any jdk on the host. Please install openjdk9 or later!')
    exit(-1)

javac_path = subprocess.check_output(['which', 'javac']).decode('utf-8').split('\n')[0]
javac_real_path = os.path.realpath(javac_path)
java_real_path = os.path.dirname(javac_real_path) + '/java'
jdk_path = os.path.dirname(os.path.dirname(javac_real_path))

javac_with_version = subprocess.check_output(['javac', '-version'], stderr=subprocess.STDOUT).decode('utf-8')
java_version = javac_with_version.split()[1].split('.')[0]

api.require('ca-certificates')
api.require('libz')
provides = ['java','java%s' % java_version]

usr_files = FileMap()

jdk_dir = os.path.basename(jdk_path)

usr_files.add(jdk_path).to('/usr/lib/jvm/java') \
    .include('lib/**') \
    .exclude('lib/security/cacerts') \
    .exclude('man/**') \
    .exclude('bin/**') \
    .include('bin/java') \
    .include('bin/jshell')

usr_files.link('/usr/lib/jvm/' + jdk_dir).to('java')
usr_files.link('/usr/lib/jvm/java/lib/security/cacerts').to('/etc/pki/java/cacerts')
usr_files.link('/usr/bin/java').to('/usr/lib/jvm/java/bin/java')
