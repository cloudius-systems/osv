from osv.modules.filemap import FileMap
from osv.modules import api, resolve
import os, os.path
import shutil
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

# On NixOS, JDK binaries have absolute /nix/store RUNPATH entries.  When
# deployed to OSv the libraries live at /usr/lib/jvm/java/lib, so we must
# patchelf the launcher binaries to use the in-VM path.
_in_vm_lib = '/usr/lib/jvm/java/lib'
_patchelf = shutil.which('patchelf')

def _patched_bin(src):
    """Return path to a copy of src with RUNPATH set to the in-VM lib dir."""
    if _patchelf is None:
        return src
    build_dir = os.path.join(resolve.get_build_path(), 'modules', 'openjdk9_1x-from-host', 'bin')
    os.makedirs(build_dir, exist_ok=True)
    dst = os.path.join(build_dir, os.path.basename(src))
    shutil.copy2(src, dst)
    os.chmod(dst, 0o755)
    subprocess.run([_patchelf, '--set-rpath', _in_vm_lib, dst], check=True)
    return dst

_java_bin = jdk_path + '/bin/java'
_jshell_bin = jdk_path + '/bin/jshell'

usr_files.add(jdk_path).to('/usr/lib/jvm/java') \
    .include('lib/**') \
    .exclude('lib/security/cacerts') \
    .exclude('man/**') \
    .exclude('bin/**')

# Add patchelf'd launcher binaries so OSv can find libjli.so at the in-VM path
usr_files.add(_patched_bin(_java_bin)).to('/usr/lib/jvm/java/bin/java')
if os.path.exists(_jshell_bin):
    usr_files.add(_patched_bin(_jshell_bin)).to('/usr/lib/jvm/java/bin/jshell')

usr_files.link('/usr/lib/jvm/' + jdk_dir).to('java')
usr_files.link('/usr/lib/jvm/java/lib/security/cacerts').to('/etc/pki/java/cacerts')
usr_files.link('/usr/bin/java').to('/usr/lib/jvm/java/bin/java')
