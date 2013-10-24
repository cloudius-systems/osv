#!/usr/bin/python

import os, sys, struct, optparse, StringIO, ConfigParser, subprocess, shutil

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
        make_option('--build-dir',
                    dest = 'zfs_root',
                    help = 'use DIR as a temporary directory',
                    metavar = 'DIR'),
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
        make_option('-s',
                    dest = 'offset',
                    help = 'offset to write the data to',
                    metavar = 'OFFSET',
                    default = 0),

])

(options, args) = opt.parse_args()

depends = StringIO.StringIO()
if options.depends:
    depends = file(options.depends, 'w')
zfs_root = options.zfs_root
#out = file(options.output, 'w')
manifest = ConfigParser.SafeConfigParser()
manifest.optionxform = str # avoid lowercasing
manifest.read(options.manifest)

depends.write('%s: \\\n' % (options.output,))


zfs_pool='osv'
zfs_fs='usr'

if os.path.exists(zfs_root):
    shutil.rmtree(zfs_root)
os.system('mkdir -p %s' % (zfs_root,))

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
        os.system('mkdir -p %s/`dirname %s`' % (zfs_root, name))
        os.system('cp -L %s %s/%s' % (hostname, zfs_root, name))

image_path = os.path.abspath(options.output)
osv = subprocess.Popen('cd ../..; scripts/run.py -i %s -e "--nomount tools/mkfs.so" --forward tcp:10000::10000' % image_path, shell = True)
nc = subprocess.Popen('sleep 3 && cd %s/usr && find -type f | cpio -o -H newc | nc -4 localhost 10000' % (zfs_root,), shell = True)

osv.wait()
nc.wait()

depends.write('\n\n')
depends.close()
