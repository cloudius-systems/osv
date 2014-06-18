from osv.modules.filemap import FileMap
from osv.modules import api
import os, os.path

usr_files = FileMap()

api.require('fonts')

jdkdir = os.path.basename(os.path.expandvars('${jdkbase}'))
if not os.path.exists('diskimage/usr/lib/jvm'):
    os.makedirs('diskimage/usr/lib/jvm')

def symlink_force(dest, name):
    if os.path.lexists(name):
        os.unlink(name)
    os.symlink(dest, name)

symlink_force('java', 'diskimage/usr/lib/jvm/' + jdkdir)
symlink_force('java/jre', 'diskimage/usr/lib/jvm/jre')

usr_files.add('${jdkbase}').to('/usr/lib/jvm/java') \
    .include('lib/**') \
    .include('jre/**') \
    .exclude('jre/lib/security/cacerts') \
    .exclude('jre/lib/audio/**')
usr_files.add(os.getcwd() + '/diskimage/usr/lib/jvm/' + jdkdir) \
    .to('/usr/lib/jvm/' + jdkdir) \
    .allow_symlink()
usr_files.add(os.getcwd() + '/diskimage/usr/lib/jvm/jre') \
    .to('/usr/lib/jvm/jre') \
    .allow_symlink()
