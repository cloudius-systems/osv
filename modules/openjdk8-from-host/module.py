from osv.modules.filemap import FileMap
from osv.modules import api
import os, os.path
import subprocess

api.require('java-cmd')
provides = ['java','java8']

#Verify that the jdk exists by trying to locate javac (java compiler)
if subprocess.call(['which', 'javac']) != 0:
     print('Could not find any jdk on the host. Please install openjdk8!')
     os.exit(-1)

java_version = subprocess.check_output(['java', '-version'], stderr=subprocess.STDOUT)
if not 'openjdk version "1.8.0' in java_version:
    print('Could not find openjdk version 8 on the host. Please install openjdk8!')
    os.exit(-1)

javac_path = subprocess.check_output(['which', 'javac']).split('\n')[0]
javac_real_path = os.path.realpath(javac_path)
jdk_path = os.path.dirname(os.path.dirname(javac_real_path))

usr_files = FileMap()

jdk_dir = os.path.basename(jdk_path)

usr_files.add(jdk_path).to('/usr/lib/jvm/java') \
    .include('jre/**') \
    .exclude('jre/lib/security/cacerts') \
    .exclude('jre/lib/amd64/*audio*') \
    .exclude('jre/lib/amd64/*sound*') \
    .exclude('') \
    .exclude('jre/man/**') \
    .exclude('jre/bin/**') \
    .include('jre/bin/java')

usr_files.link('/usr/lib/jvm/' + jdk_dir).to('java')
usr_files.link('/usr/lib/jvm/jre').to('java/jre')
usr_files.link('/usr/lib/jvm/java/jre/lib/security/cacerts').to('/etc/pki/java/cacerts')
usr_files.link('/usr/bin/java').to('/usr/lib/jvm/java/jre/bin/java')
