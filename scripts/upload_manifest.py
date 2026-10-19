#!/usr/bin/python3

import optparse, os, shutil, signal, subprocess, socket, tempfile, threading, stat, sys, time
from manifest_common import add_var, expand, unsymlink, read_manifest, defines, strip_file
from contextlib import closing

import io
StringIO = io.StringIO

host_arch = os.uname().machine

def find_free_port():
    with closing(socket.socket(socket.AF_INET, socket.SOCK_STREAM)) as s:
        s.bind(('localhost', 0))
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        return s.getsockname()[1]

# cpiod --ready sends this once it has accepted our connection.
CPIOD_GREETING = b"CPIOD1\n"

def wait_for_cpiod(osv, port, deadline_s=300):
    # QEMU's hostfwd accepts connect() as soon as QEMU starts, whether or not
    # cpiod is listening in the guest yet, so a successful connect proves
    # nothing. Wait for cpiod's greeting instead. If the guest refuses the
    # forwarded connection (not listening yet), the host socket is closed and
    # we retry. Do not give up on a connection that is merely slow: closing
    # it early could leave the guest a stale connection that consumes
    # cpiod's only accept(), so each attempt waits up to the overall deadline.
    import time as _time
    deadline = _time.monotonic() + deadline_s
    while _time.monotonic() < deadline:
        if osv.poll() is not None:
            sys.exit("qemu failed.")
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            s.connect(("127.0.0.1", port))
            got = b""
            while len(got) < len(CPIOD_GREETING):
                s.settimeout(max(0.1, deadline - _time.monotonic()))
                chunk = s.recv(len(CPIOD_GREETING) - len(got))
                if not chunk:
                    break
                got += chunk
            if got == CPIOD_GREETING:
                s.settimeout(None)
                return s
            if got:
                s.close()
                sys.exit("ERROR: unexpected greeting from cpiod: %r" % got)
        except (ConnectionRefusedError, ConnectionResetError, socket.timeout,
                OSError):
            pass
        s.close()
        _time.sleep(1)
    sys.exit("Timed out waiting for cpiod on port %d" % port)

def upload(osv, manifest, depends, port):
    manifest = [(x, y % defines) for (x, y) in manifest]
    files = list(expand(manifest))
    files = [(x, unsymlink(y)) for (x, y) in files]

    # We'll want to read the rest of the guest's output, so that it doesn't
    # hang, and so the user can see what's happening. Easiest to do this with
    # a thread. Start it before waiting for cpiod, so the guest's boot and
    # mkfs output cannot fill the pipe while we wait.
    stop_output = False
    def consumeoutput(file, silent):
        for line in iter(lambda: file.readline(), b''):
            if not silent():
                os.write(sys.stdout.fileno(), line)
    # A daemon, so a failed upload can exit without waiting for QEMU.
    threading.Thread(target=consumeoutput, args=(osv.stdout, lambda: stop_output),
                     daemon=True).start()

    s = wait_for_cpiod(osv, port)

    # Send a CPIO header or file, padded to multiple of 4 bytes
    def cpio_send(data):
        s.sendall(data)
        partial = len(data)%4
        if partial > 0:
            s.sendall(b'\0'*(4-partial))
    def cpio_field(number, length):
        return ("%.*x" % (length, number)).encode()
    def cpio_header(filename, mode, filesize):
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

def read_pid(pidfile):
    try:
        with open(pidfile) as f:
            return int(f.read())
    except (OSError, ValueError):
        return None

def kill_qemu(osv, pidfile):
    # QEMU is a grandchild (sh, then run.py), so stopping osv alone could
    # leave it running. Signal it by the pid it wrote; run.py then exits.
    # Only while osv runs: run.py normally has not reaped QEMU by then,
    # so the pid in the file is still QEMU's. An early failure can come
    # before QEMU has written the file, so give it a moment to appear.
    pid = read_pid(pidfile)
    for _ in range(50):
        if pid is not None or osv.poll() is not None:
            break
        time.sleep(0.1)
        pid = read_pid(pidfile)
    if pid is not None and osv.poll() is None:
        try:
            os.kill(pid, signal.SIGTERM)
        except OSError:
            pass
    try:
        osv.wait(timeout=10)
    except subprocess.TimeoutExpired:
        osv.kill()
        osv.wait()

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
            make_option('--arch',
                        dest='arch',
                        default=host_arch,
                        help="specify QEMU architecture: x86_64, aarch64")
    ])

    (options, args) = opt.parse_args()

    depends = StringIO()
    if options.depends:
        depends = file(options.depends, 'w')
    manifest = read_manifest(options.manifest)

    depends.write('%s: \\\n' % (options.output,))

    image_path = os.path.abspath(options.output)
    upload_port = find_free_port()
    arch = options.arch
    if arch == 'x64':
        arch = 'x86_64'

    if arch == 'aarch64':
        console = ''
        zfs_builder_name = 'zfs_builder.img'
    else:
        console = '--console=serial'
        zfs_builder_name = 'zfs_builder-stripped.elf'

    # Drain the pool before exporting it.  The zfs_builder's libzfs keeps an
    # empty /etc/mnttab (OSv's mount shim never populates it), so
    # "zpool export"'s own zpool_disable_datasets() finds no mounts to unmount
    # and the kernel then refuses the export with EBUSY ("pool is busy")
    # because the auto-mounted datasets still hold the objsets.  Unmount the
    # datasets explicitly by mountpoint (children first) with the OSv-native
    # /tools/umount.so, which calls umount(2) -> VFS_UNMOUNT -> the real
    # zfs_umount -> dmu_objset_disown, dropping the spa refcount to zero so the
    # export succeeds cleanly.  (We cannot use "zfs unmount" here: the bsd
    # zfs.so command needs statfs2mnttab, which the builder image does not
    # provide.)  Commands are ';'-separated and run independently, so a
    # spurious unmount does not abort the chain.
    zfs_builder_cmd = ('%s --norandom --nomount --noinit --preload-zfs-library '
        '/tools/mkfs.so; /tools/cpiod.so --ready --prefix /zfs/; '
        '/zfs.so set compression=off osv; '
        '/tools/umount.so /zfs/zfs; /tools/umount.so /zfs; '
        '/zpool.so export osv') % console
    piddir = tempfile.mkdtemp(prefix='osv-upload-')
    pidfile = os.path.join(piddir, 'qemu.pid')
    osv = subprocess.Popen('cd ../..; scripts/run.py -k --kernel-path build/last/%s --arch=%s --vnc none -m 4G -c1 -i "%s" --block-device-cache writeback -s -e "%s" --forward tcp:127.0.0.1:%s-:10000 --pass-args=\"-pidfile %s\"' % (zfs_builder_name,arch,image_path,zfs_builder_cmd,upload_port,pidfile), shell=True, stdout=subprocess.PIPE)

    # A failed upload leaves cpiod waiting for a connection that will not
    # come, so QEMU would never exit. Kill it and fail the build instead.
    # (--pass-args needs the "=" form: its value starts with "-".)
    try:
        upload(osv, manifest, depends, upload_port)
    except BaseException as e:
        kill_qemu(osv, pidfile)
        if isinstance(e, OSError):
            sys.exit("ERROR: zfs_builder upload failed (%s); QEMU killed. "
                     "cpiod may not have been listening yet." % e)
        raise
    finally:
        shutil.rmtree(piddir, ignore_errors=True)

    # cpiod acknowledged the sync, so the ZFS image is written. Give QEMU up
    # to 120 seconds to exit cleanly (e.g. via ACPI poweroff), then kill it.
    # OSv may call halt() instead of poweroff() when ACPI is not available
    # (e.g. --noinit suppresses ACPI init), so QEMU would never exit on its own.
    try:
        ret = osv.wait(timeout=120)
    except subprocess.TimeoutExpired:
        osv.terminate()
        try:
            osv.wait(timeout=5)
        except subprocess.TimeoutExpired:
            osv.kill()
            osv.wait()
        ret = 0  # upload succeeded; QEMU just didn't exit cleanly
    if ret != 0:
        sys.exit("Upload failed.")

    depends.write('\n\n')
    depends.close()

if __name__ == "__main__":
    main()
