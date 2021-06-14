from osv.modules.filemap import FileMap
from osv.modules import api
import os, os.path
import subprocess

host_arch = os.uname().machine
if host_arch == 'aarch64':
    arch_dir = 'aarch64'
elif host_arch == 'x86_64':
    arch_dir = 'amd64'
else:
    print('Unsupported architecture: %s' % host_arch)
    exit(-1)

api.require('java-cmd')
provides = ['java','java8']

#Verify that the jdk exists by trying to locate javac (java compiler)
if subprocess.call(['which', 'javac']) != 0:
    print('Could not find any jdk on the host. Please install openjdk8!')
    exit(-1)

javac_path = subprocess.check_output(['which', 'javac']).decode('utf-8').split('\n')[0]
javac_real_path = os.path.realpath(javac_path)
java_real_path = os.path.dirname(javac_real_path) + '/java'
jdk_path = os.path.dirname(os.path.dirname(javac_real_path))

java_version = subprocess.check_output([java_real_path, '-version'], stderr=subprocess.STDOUT).decode('utf-8')
if not 'openjdk version "1.8.0' in java_version:
    print('Could not find openjdk version 8 on the host. Please install openjdk8!')
    exit(-1)

usr_files = FileMap()

jdk_dir = os.path.basename(jdk_path)

usr_files.add(jdk_path).to('/usr/lib/jvm/java') \
    .include('jre/**') \
    .exclude('jre/lib/security/cacerts') \
    .exclude('jre/lib/%s/*audio*' % arch_dir) \
    .exclude('jre/lib/%s/*sound*' % arch_dir) \
    .exclude('') \
    .exclude('jre/man/**') \
    .exclude('jre/bin/**') \
    .include('jre/bin/java')

usr_files.link('/usr/lib/jvm/' + jdk_dir).to('java')
usr_files.link('/usr/lib/jvm/jre').to('java/jre')
usr_files.link('/usr/lib/jvm/java/jre/lib/security/cacerts').to('/etc/pki/java/cacerts')
usr_files.link('/usr/bin/java').to('/usr/lib/jvm/java/jre/bin/java')
