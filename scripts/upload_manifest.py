#!/usr/bin/python

import os, optparse, io, subprocess, socket, threading, stat, sys, re

try:
    import StringIO
    # This works on Python 2
    StringIO = StringIO.StringIO
except ImportError:
    # This works on Python 3
    StringIO = io.StringIO

defines = {}

def add_var(option, opt, value, parser):
    var, val = value.split('=')
    defines[var] = val

def expand(items):
    for name, hostname in items:
        if name.endswith('/**') and hostname.endswith('/**'):
            name = name[:-2]
            hostname = hostname[:-2]
            for dirpath, dirnames, filenames in os.walk(hostname):
                for filename in filenames:
                    relpath = dirpath[len(hostname):]
                    if relpath != "":
                        relpath += "/"
                    yield (name + relpath + filename,
                           hostname + relpath + filename)
        elif '/&/' in name and hostname.endswith('/&'):
            prefix, suffix = name.split('/&/', 1)
            yield (prefix + '/' + suffix, hostname[:-1] + suffix)
        else:
            yield (name, hostname)

def unsymlink(f):
    if f.startswith('!'):
        return f[1:]
    if f.startswith('->'):
        return f
    try:
        link = os.readlink(f)
        if link.startswith('/'):
            # try to find a match
            base = os.path.dirname(f)
            while not os.path.exists(base + link):
                if base == '/':
                    return f
                base = os.path.dirname(base)
        else:
            base = os.path.dirname(f) + '/'
        return unsymlink(base + link)
    except Exception:
        return f

# Reads the manifest and returns it as a list of pairs (guestpath, hostpath).
def read_manifest(fn):
    ret = []
    comment = re.compile("^[ \t]*(|#.*|\[manifest])$")
    with open(fn, 'r') as f:
        for line in f:
            line = line.rstrip();
            if comment.match(line): continue
            components = line.split(": ", 2)
            guestpath = components[0].strip();
            hostpath = components[1].strip()
            ret.append((guestpath, hostpath))
    return ret

def upload(osv, manifest, depends):
    files = list(expand(manifest))
    files = [(x, unsymlink(y % defines)) for (x, y) in files]

    # Wait for the guest to come up and tell us it's listening
    while True:
        line = osv.stdout.readline()
        if not line or line.find(b"Waiting for connection") >= 0:
            break
        os.write(sys.stdout.fileno(), line)

    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect(("127.0.0.1", 10000))

    # We'll want to read the rest of the guest's output, so that it doesn't
    # hang, and so the user can see what's happening. Easiest to do this with
    # a thread.
    def consumeoutput(file):
        for line in iter(lambda: file.readline(), b''):
            os.write(sys.stdout.fileno(), line)
    threading.Thread(target=consumeoutput, args=(osv.stdout,)).start()

    # Send a CPIO header or file, padded to multiple of 4 bytes
    def cpio_send(data):
        s.sendall(data)
        partial = len(data)%4
        if partial > 0:
            s.sendall(b'\0'*(4-partial))
    def cpio_field(number, length):
        return ("%.*x" % (length, number)).encode()
    def cpio_header(filename, mode, filesize):
        if sys.version_info >= (3, 0, 0):
            filename = filename.encode("utf-8")
        return (b"070701"                         # magic
                + cpio_field(0, 8)                # inode
                + cpio_field(mode, 8)             # mode
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
                + filename + b'\0')

    def strip_file(filename):
        stripped_filename = filename
        if filename.endswith(".so") and \
                (filename[0] != "/" or filename.startswith(os.getcwd())):
            stripped_filename = filename[:-3] + "-stripped.so"
            if not os.path.exists(stripped_filename) \
                    or (os.path.getmtime(stripped_filename) < \
                        os.path.getmtime(filename)):
                subprocess.call(["strip", "-o", stripped_filename, filename])
        return stripped_filename


    # Send the files to the guest
    for name, hostname in files:
        if hostname.startswith("->"):
            link = hostname[2:]
            cpio_send(cpio_header(name, stat.S_IFLNK, len(link)))
            cpio_send(link.encode())
        else:
            depends.write('\t%s \\\n' % (hostname,))
            hostname = strip_file(hostname)
            if os.path.islink(hostname):
                perm = os.lstat(hostname).st_mode & 0o777
                link = os.readlink(hostname)
                cpio_send(cpio_header(name, perm | stat.S_IFLNK, len(link)))
                cpio_send(link.encode())
            elif os.path.isdir(hostname):
                perm = os.stat(hostname).st_mode & 0o777
                cpio_send(cpio_header(name, perm | stat.S_IFDIR, 0))
            else:
                perm = os.stat(hostname).st_mode & 0o777
                cpio_send(cpio_header(name, perm | stat.S_IFREG, os.stat(hostname).st_size))
                with open(hostname, 'rb') as f:
                    cpio_send(f.read())
    cpio_send(cpio_header("TRAILER!!!", 0, 0))
    s.shutdown(socket.SHUT_WR)

    # Wait for the guest to actually finish writing and syncing
    s.recv(1)
    s.close()

def main():
    make_option = optparse.make_option

    opt = optparse.OptionParser(option_list=[
            make_option('-o',
                        dest='output',
                        help='write to FILE',
                        metavar='FILE'),
            make_option('-d',
                        dest='depends',
                        help='write dependencies to FILE',
                        metavar='FILE',
                        default=None),
            make_option('-m',
                        dest='manifest',
                        help='read manifest from FILE',
                        metavar='FILE'),
            make_option('-D',
                        type='string',
                        help='define VAR=DATA',
                        metavar='VAR=DATA',
                        action='callback',
                        callback=add_var),
    ])

    (options, args) = opt.parse_args()

    depends = StringIO()
    if options.depends:
        depends = file(options.depends, 'w')
    manifest = read_manifest(options.manifest)

    depends.write('%s: \\\n' % (options.output,))

    image_path = os.path.abspath(options.output)
    osv = subprocess.Popen('cd ../..; scripts/run.py --vnc none -m 512 -c1 -i %s -u -s -e "--norandom --noinit /tools/cpiod.so" --forward tcp:10000::10000' % image_path, shell=True, stdout=subprocess.PIPE)

    upload(osv, manifest, depends)

    osv.wait()

    # Disable ZFS compression; it stops taking effect from this point on.
    osv = subprocess.Popen('cd ../..; scripts/run.py -m 512 -c1 -i %s -u -s -e "--norandom --noinit /zfs.so set compression=off osv"' % image_path, shell=True, stdout=subprocess.PIPE)
    osv.wait()

    depends.write('\n\n')
    depends.close()

if __name__ == "__main__":
    main()
