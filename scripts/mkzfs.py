#!/usr/bin/python

import os, sys, struct, optparse, StringIO, ConfigParser, subprocess, shutil, socket, time, threading

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
#out = file(options.output, 'w')
manifest = ConfigParser.SafeConfigParser()
manifest.optionxform = str # avoid lowercasing
manifest.read(options.manifest)

depends.write('%s: \\\n' % (options.output,))


zfs_pool='osv'
zfs_fs='usr'


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

image_path = os.path.abspath(options.output)
osv = subprocess.Popen('cd ../..; scripts/run.py -c1 -i %s -g -e "--nomount tools/mkfs.so" --forward tcp:10000::10000' % image_path, shell = True, stdout=subprocess.PIPE)

# Wait for the guest to come up and tell us it's listening
while True:
    line = osv.stdout.readline()
    if not line or line.find("Waiting for connection")>=0:
        break;
    print line.rstrip();

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.connect(("127.0.0.1", 10000));

# We'll want to read the rest of the guest's output, so that it doesn't
# hang, and so the user can see what's happening. Easiest to do this with
# a thread.
def consumeoutput(file):
    for line in iter(file.readline, ''):
        print line.rstrip()
threading.Thread(target = consumeoutput, args = (osv.stdout,)).start()

# Send a CPIO header or file, padded to multiple of 4 bytes 
def cpio_send(data):
    s.sendall(data)
    partial = len(data)%4
    if partial > 0:
        s.sendall('\0'*(4-partial))
def cpio_field(number, length):
    return "%.*x" % (length, number);
def cpio_header(filename, filesize):
    return ("070701"                          # magic
            + cpio_field(0, 8)                # inode
            + cpio_field(0, 8)                # mode
            + cpio_field(0, 8)                # uid
            + cpio_field(0, 8)                # gid
            + cpio_field(0, 8)                # nlink
            + cpio_field(0, 8)                # mtime
            + cpio_field(filesize, 8)         # filesize
            + cpio_field(0, 8)                # devmajor
            + cpio_field(0, 8)                # devminor
            + cpio_field(0, 8)                # rdevmajor
            + cpio_field(0, 8)                # rdevminor
            + cpio_field(len(filename)+1, 8)  # namesize
            + cpio_field(0, 8)                # check
            + filename + '\0')

# Send the files to the guest
for name, hostname in files:
    depends.write('\t%s \\\n' % (hostname,))
    if name[:4] in [ '/usr' ]:
        name = name[5:] # mkfs.so puts everything in /usr
        cpio_send(cpio_header(name, os.stat(hostname).st_size))
        with open(hostname, 'r') as f:
            cpio_send(f.read())
cpio_send(cpio_header("TRAILER!!!", 0))
s.shutdown(socket.SHUT_WR)

# Wait for the guest to actually finish writing and syncing
s.recv(1)
s.close()

osv.wait()

depends.write('\n\n')
depends.close()
