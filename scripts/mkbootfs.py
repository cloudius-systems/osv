#!/usr/bin/python

import os, sys, struct, optparse, io
try:
    import configparser
except ImportError:
    import ConfigParser as configparser

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

metadata_size = 128
depends = io.StringIO()
if options.depends:
    depends = open(options.depends, 'w')
out = open(options.output, 'wb')
manifest = configparser.SafeConfigParser()
manifest.optionxform = str # avoid lowercasing
manifest.read(options.manifest)

depends.write(u'%s: \\\n' % (options.output,))

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

pos = (len(files) + 1) * metadata_size

for name, hostname in files:
    size = os.stat(hostname).st_size
    metadata = struct.pack('QQ112s', size, pos, name.encode())
    out.write(metadata)
    pos += size
    depends.write(u'\t%s \\\n' % (hostname,))

out.write(struct.pack('128s', b''))

for name, hostname in files:
    out.write(open(hostname, 'rb').read())

depends.write(u'\n\n')

out.close()
depends.close()
