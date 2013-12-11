#!/usr/bin/python2

import os, sys, struct, optparse, StringIO, ConfigParser, subprocess, shutil, socket, time, threading
import upload_manifest

make_option = optparse.make_option

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
])

(options, args) = opt.parse_args()

depends = StringIO.StringIO()
if options.depends:
    depends = file(options.depends, 'w')

manifest = ConfigParser.SafeConfigParser()
manifest.optionxform = str # avoid lowercasing
manifest.read(options.manifest)

depends.write('%s: \\\n' % (options.output,))

image_path = os.path.abspath(options.output)

osv = subprocess.Popen('cd ../..; scripts/run.py -c1 -i %s -u -s -e "--nomount tools/mkfs.so; tools/cpiod.so --prefix /zfs/zfs" --forward tcp:10000::10000' % image_path, shell = True, stdout=subprocess.PIPE)

upload_manifest.upload(osv, manifest, depends)

osv.wait()

depends.write('\n\n')
depends.close()
