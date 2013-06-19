#!/usr/bin/python

import os, sys, struct, optparse, StringIO, ConfigParser

make_option = optparse.make_option

defines = {}
def add_var(option, opt, value, parser):
    var, val = value.split('=')
    defines[var] = val

opt = optparse.OptionParser(option_list = [
        make_option('-o',
                    dest = 'output',
                    help = 'write to FILE',
                    metavar = 'FILE'),
        make_option('-d',
                    dest = 'depends',
                    help = 'write dependencies to FILE',
                    metavar = 'FILE',
                    default = None),
        make_option('-m',
                    dest = 'manifest',
                    help = 'read manifest from FILE',
                    metavar = 'FILE'),
        make_option('-D',
                    type = 'string',
                    help = 'define VAR=DATA',
                    metavar = 'VAR=DATA',
                    action = 'callback',
                    callback = add_var),
])

(options, args) = opt.parse_args()

depends = StringIO.StringIO()
if options.depends:
    depends = file(options.depends, 'w')
#out = file(options.output, 'w')
manifest = ConfigParser.SafeConfigParser()
manifest.optionxform = str # avoid lowercasing
manifest.read(options.manifest)

depends.write('%s: \\\n' % (options.output,))


zfs_root='/zfs'
loop_dev='/dev/loop7'
dev='/dev/vblk1'
zfs_pool='osv'
zfs_fs='usr'

os.system('sudo mkdir -p %s' % zfs_root)

os.system('sudo rm -f %s' % options.output)
os.system('sudo truncate --size 10g %s' % options.output)
os.system('sudo losetup %s %s' % (loop_dev, options.output))
os.system('sudo ln %s %s' % (loop_dev, dev))

os.system('sudo zpool create -f %s -R %s %s' % (zfs_pool, zfs_root, dev))
os.system('sudo zfs create %s/%s' % (zfs_pool, zfs_fs))

files = dict([(f, manifest.get('manifest', f, vars = defines))
              for f in manifest.options('manifest')])

def expand(items):
    for name, hostname in items:
        if name.endswith('/**') and hostname.endswith('/**'):
            name = name[:-2]
            hostname = hostname[:-2]
            for dirpath, dirnames, filenames in os.walk(hostname):
                for filename in filenames:
                    relpath = dirpath[len(hostname):]
                    if relpath != "" :
                        relpath += "/"
                    yield (name + relpath + filename,
                           hostname + relpath + filename)
        elif '/&/' in name and hostname.endswith('/&'):
            prefix, suffix = name.split('/&/', 1)
            yield (prefix + '/' + suffix, hostname[:-1] + suffix)
        else:
            yield (name, hostname)

def unsymlink(f):
    try:
        link = os.readlink(f)
        if link.startswith('/'):
            # try to find a match
            base = os.path.dirname(f)
            while not os.path.exists(base + link):
                base = os.path.dirname(base)
        else:
            base = os.path.dirname(f) + '/'
        return unsymlink(base + link)
    except Exception:
        return f

files = list(expand(files.items()))
files = [(x, unsymlink(y)) for (x, y) in files]

for name, hostname in files:
    depends.write('\t%s \\\n' % (hostname,))
    if name[:4] in [ '/usr' ]:
        os.system('sudo mkdir -p %s/`dirname %s`' % ('/zfs/', name))
        os.system('sudo cp -L %s %s/%s' % (hostname, '/zfs/', name))

os.system('sudo zpool export %s' % zfs_pool)
os.system('sleep 2')
os.system('sudo losetup -d %s' % loop_dev)
os.system('sudo rm %s' % dev)

depends.write('\n\n')
depends.close()
