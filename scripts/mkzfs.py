#!/usr/bin/python

import os, sys, struct, optparse, io, subprocess, shutil, socket, time, threading
try:
    import configparser
except ImportError:
    import ConfigParser as configparser
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

depends = io.StringIO()
if options.depends:
    depends = open(options.depends, 'w')

manifest = configparser.SafeConfigParser()
manifest.optionxform = str # avoid lowercasing
manifest.read(options.manifest)

depends.write(u'%s: \\\n' % (options.output,))

image_path = os.path.abspath(options.output)

osv = subprocess.Popen('cd ../..; scripts/run.py --vnc none -c1 -m 512 -i %s -u -s -e "--nomount tools/mkfs.so; tools/cpiod.so --prefix /zfs/zfs" --forward tcp:10000::10000' % image_path, shell = True, stdout=subprocess.PIPE)

upload_manifest.upload(osv, manifest, depends)

osv.wait()

depends.write(u'\n\n')
depends.close()
