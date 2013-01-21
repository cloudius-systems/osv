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

metadata_size = 128
depends = StringIO.StringIO()
if options.depends:
    depends = file(options.depends, 'w')
out = file(options.output, 'w')
manifest = ConfigParser.SafeConfigParser()
manifest.optionxform = str # avoid lowercasing
manifest.read(options.manifest)

depends.write('%s: \\\n' % (options.output,))

files = dict([(f, manifest.get('manifest', f, vars = defines))
              for f in manifest.options('manifest')])
pos = (len(files) + 1) * metadata_size

for name, hostname in files.items():
    size = os.stat(hostname).st_size
    metadata = struct.pack('QQ112s', size, pos, name)
    out.write(metadata)
    pos += size
    depends.write('\t%s \\\n' % (hostname,))

out.write(struct.pack('128s', ''))

for name, hostname in files.items():
    out.write(file(hostname).read())

depends.write('\n\n')

out.close()
depends.close()
