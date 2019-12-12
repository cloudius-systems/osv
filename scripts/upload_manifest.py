#!/usr/bin/python3

import optparse, os, subprocess, socket, threading, stat, sys
from manifest_common import add_var, expand, unsymlink, read_manifest, defines, strip_file
from contextlib import closing

import io
StringIO = io.StringIO

def find_free_port():
    with closing(socket.socket(socket.AF_INET, socket.SOCK_STREAM)) as s:
        s.bind(('localhost', 0))
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        return s.getsockname()[1]

def upload(osv, manifest, depends, port):
    manifest = [(x, y % defines) for (x, y) in manifest]
    files = list(expand(manifest))
    files = [(x, unsymlink(y)) for (x, y) in files]

    # Wait for the guest to come up and tell us it's listening
    while True:
        line = osv.stdout.readline()
        if not line or line.find(b"Waiting for connection") >= 0:
            break
        os.write(sys.stdout.fileno(), line)

    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect(("127.0.0.1", port))

    # We'll want to read the rest of the guest's output, so that it doesn't
    # hang, and so the user can see what's happening. Easiest to do this with
    # a thread.
    stop_output = False
    def consumeoutput(file, silent):
        for line in iter(lambda: file.readline(), b''):
            if not silent():
                os.write(sys.stdout.fileno(), line)
    threading.Thread(target=consumeoutput, args=(osv.stdout, lambda: stop_output)).start()

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

    # Send the files to the guest
    try:
        for name, hostname in files:
            if hostname.startswith("->"):
                link = hostname[2:]
                cpio_send(cpio_header(name, stat.S_IFLNK, len(link)))
                cpio_send(link.encode())
            else:
                depends.write('\t%s \\\n' % (hostname,))
                if hostname.endswith("-stripped.so"):
                    continue
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
    except:
        # We had an error uploading one of the files (e.g., one of the files
        # in the manifest does not exist). Let's print an error message, but
        # first stop the consumeoutput thread displaying the guest's output
        # so its messages do not get mixed with ours. Then, do NOT exit -
        # continue the code below to stop the guest normally.
        import time
        time.sleep(1) # give consumeoutput thread a chance to print some more
        stop_output = True
        print("\nERROR: file upload failed:")
        print(sys.exc_info()[1])
    cpio_send(cpio_header("TRAILER!!!", 0, 0))
    s.shutdown(socket.SHUT_WR)

    # Wait for the guest to actually finish writing and syncing
    s.recv(1)
    s.close()
    if stop_output:
        # stop_output is set when we failed during the upload, so let's
        # have the upload fail.
        sys.exit(1)

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
    upload_port = find_free_port()
    osv = subprocess.Popen('cd ../..; scripts/run.py --vnc none -m 512 -c1 -i "%s" --block-device-cache unsafe -s -e "--norandom --nomount --noinit /tools/mkfs.so; /tools/cpiod.so --prefix /zfs/zfs/; /zfs.so set compression=off osv" --forward tcp:127.0.0.1:%s-:10000' % (image_path,upload_port), shell=True, stdout=subprocess.PIPE)

    upload(osv, manifest, depends, upload_port)

    ret = osv.wait()
    if ret != 0:
        sys.exit("Upload failed.")

    depends.write('\n\n')
    depends.close()

if __name__ == "__main__":
    main()
