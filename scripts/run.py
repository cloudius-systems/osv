#!/usr/bin/env python
from __future__ import print_function
import subprocess
import sys
import argparse
import os
import tempfile
import errno
import re

stty_params = None

devnull = open('/dev/null', 'w')

def stty_save():
    global stty_params
    p = subprocess.Popen(["stty", "-g"], stdout=subprocess.PIPE, stderr=devnull)
    stty_params, err = p.communicate()
    stty_params = stty_params.strip()

def stty_restore():
    if stty_params:
        subprocess.call(["stty", stty_params], stderr=devnull)

def cleanups():
    "cleanups after execution"
    stty_restore()

def format_args(args):
    def format_arg(arg):
        if ' ' in arg:
            return '"%s"' % arg
        return arg

    return ' '.join(map(format_arg, args))

def set_imgargs(options):
    execute = options.execute
    if options.image and not execute:
        return
    if not execute:
        with open("build/%s/cmdline" % (options.opt_path), "r") as cmdline:
            execute = cmdline.read()
    if options.verbose:
        execute = "--verbose " + execute

    if options.jvm_debug or options.jvm_suspend:
        if '-agentlib:jdwp' in execute:
            raise Exception('The command line already has debugger options')
        if not 'java.so' in execute:
            raise Exception('java.so is not part of the command line')

        debug_options = '-agentlib:jdwp=transport=dt_socket,server=y,suspend=%s,address=5005' % \
            ('n', 'y')[options.jvm_suspend]
        execute = execute.replace('java.so', 'java.so ' + debug_options)

    if options.trace:
        execute = ' '.join('--trace=%s' % name for name in options.trace) + ' ' + execute

    if options.trace_backtrace:
        execute = '--trace-backtrace ' + execute

    if options.sampler:
        execute = '--sampler=%d %s' % (int(options.sampler), execute)

    cmdline = ["scripts/imgedit.py", "setargs", options.image_file, execute]
    if options.dry_run:
        print(format_args(cmdline))
    else:
        subprocess.call(cmdline)

def is_direct_io_supported(path):
    if not os.path.exists(path):
        raise Exception('Path not found: ' + path)

    try:
        file = os.open(path, os.O_RDONLY | os.O_DIRECT)
        os.close(file)
        return True
    except OSError as e:
        if e.errno == errno.EINVAL:
            return False
        raise

def start_osv_qemu(options):

    if options.unsafe_cache or not is_direct_io_supported(options.image_file):
        aio = 'cache=unsafe,aio=threads'
    else:
        aio = 'cache=none,aio=native'

    args = [
        "-m", options.memsize,
        "-smp", options.vcpus]

    if not options.novnc:
        args += [
        "-vnc", options.vnc]
    else:
        args += [
        "--nographic"]

    if not options.nogdb:
        args += [
        "-gdb", "tcp::%s,server,nowait" % options.gdb]

    if options.graphics:
        args += [
        "-display", "sdl"]

    if options.sata:
        args += [
        "-machine", "q35",
        "-drive", "file=%s,if=none,id=hd0,media=disk,%s" % (options.image_file, aio),
        "-device", "ide-hd,drive=hd0,id=idehd0,bus=ide.0"]
    elif options.scsi:
        args += [
        "-device", "virtio-scsi-pci,id=scsi0",
        "-drive", "file=%s,if=none,id=hd0,media=disk,%s" % (options.image_file, aio),
        "-device", "scsi-hd,bus=scsi0.0,drive=hd0,scsi-id=1,lun=0,bootindex=0"]
    elif options.ide:
        args += [
        "-hda", options.image_file]
    else:
        args += [
        "-device", "virtio-blk-pci,id=blk0,bootindex=0,drive=hd0,scsi=off",
        "-drive", "file=%s,if=none,id=hd0,%s" % (options.image_file, aio)]

    if options.no_shutdown:
        args += ["-no-reboot", "-no-shutdown"]

    if options.wait:
        args += ["-S"]

    for idx in range(int(options.nics)):
        if options.vmxnet3:
            net_device_options = ['vmxnet3']
        else:
            net_device_options = ['virtio-net-pci']

        if options.mac:
            net_device_options.append('mac=%s' % options.mac)

        if options.networking:
            if options.vhost:
                args += ["-netdev", "tap,id=hn%d,script=scripts/qemu-ifup.sh,vhost=on" % idx]
            else:
                for bridge_helper_dir in ['/usr/libexec', '/usr/lib/qemu']:
                    bridge_helper = bridge_helper_dir + '/qemu-bridge-helper'
                    if os.path.exists(bridge_helper):
                       break
                else:
                    print("Unable to find qemu-bridge-helper program", file=sys.stderr)
                    return
                args += ["-netdev", "bridge,id=hn%d,br=%s,helper=%s" % (idx, options.bridge, bridge_helper)]
            net_device_options.extend(['netdev=hn%d' % idx, 'id=nic%d' % idx])
        else:
            args += ["-netdev", "user,id=un%d,net=192.168.122.0/24,host=192.168.122.1" % idx]
            net_device_options.append("netdev=un%d" % idx)

        args += ["-device", ','.join(net_device_options)]

    if options.api:
        args += ["-redir", "tcp:8000::8000"]

    for rule in options.forward:
        args += ['-redir', rule]

    args += ["-device", "virtio-rng-pci"]

    if options.hypervisor == "kvm":
        args += ["-enable-kvm", "-cpu", "host,+x2apic"]
    elif options.hypervisor == "none" or options.hypervisor == "qemu":
        pass

    if options.detach:
        args += ["-daemonize"]
    else:
        signal_option = ('off', 'on')[options.with_signals]
        args += ["-chardev", "stdio,mux=on,id=stdio,signal=%s" % signal_option]
        args += ["-mon", "chardev=stdio,mode=readline,default"]
        args += ["-device", "isa-serial,chardev=stdio"]

    for a in options.pass_args or []:
        args += a.split()

    try:
        # Save the current settings of the stty
        stty_save()

        # Launch qemu
        qemu_env = os.environ.copy()

        qemu_env['OSV_BRIDGE'] = options.bridge
        cmdline = [options.qemu_path] + args
        if options.dry_run:
            print(format_args(cmdline))
        else:
            subprocess.call(cmdline, env=qemu_env)
    except OSError as e:
        if e.errno == errno.ENOENT:
            print("'qemu-system-x86_64' binary not found. Please install the qemu-system-x86 package.")
        else:
            print("OS error({0}): \"{1}\" while running qemu-system-x86_64 {2}".
                format(e.errno, e.strerror, " ".join(args)))
    finally:
        cleanups()

def start_osv_xen(options):
    if options.hypervisor == "xen":
        args = [
            "builder='hvm'",
            "xen_platform_pci=1",
            "acpi=1",
            "apic=1",
            "boot='c'",
        ]
    else:
        args = ["kernel='%s/build/%s/loader.elf'" % (os.getcwd(), options.opt_path)]

    try:
        memory = int(options.memsize)
    except ValueError:
        memory = options.memsize

        if memory[-1:].upper() == "M":
            memory = int(memory[:-1])
        elif memory[-2:].upper() == "MB":
            memory = int(memory[:-2])
        elif memory[-1:].upper() == "G":
            memory = 1024 * int(memory[:-1])
        elif memory[-2:].upper() == "GB":
            memory = 1024 * int(memory[:-2])
        else:
            print("Unrecognized memory size", file=sys.stderr)
            return

    if not options.novnc:
        vncoptions = re.match("^(?P<vncaddr>[^:]*):?(?P<vncdisplay>[0-9]*$)", options.vnc)

        if not vncoptions:
            raise Exception('Invalid vnc option format: \"' + options.vnc + "\"")

        if vncoptions.group("vncaddr"):
            args += ["vnclisten=%s" % (vncoptions.group("vncaddr"))]

        if vncoptions.group("vncdisplay"):
            args += ["vncdisplay=%s" % (vncoptions.group("vncdisplay"))]

    args += [
        "memory=%d" % (memory),
        "vcpus=%s" % (options.vcpus),
        "maxcpus=%s" % (options.vcpus),
        "name='osv-%d'" % (os.getpid()),
        "disk=['/dev/loop%s,raw,hda,rw']" % os.getpid(),
        "serial='pty'",
        "paused=0",
        "on_crash='preserve'"
    ]

    if options.networking:
       net_device_options = "bridge=%s" % options.bridge
       if options.mac:
          net_device_options += ",mac=%s" % options.mac
       if options.script:
           net_device_options += ",script=%s" % options.script
       args += ["vif=['%s']" % (net_device_options)]

    # Using xm would allow us to get away with creating the file, but it comes
    # with its set of problems as well. Stick to xl.
    xenfile = tempfile.NamedTemporaryFile(mode="w")
    xenfile.writelines("%s\n" % item for item in args)
    xenfile.flush()

    try:
        # Save the current settings of the stty
        stty_save()

        #create a loop device backed by image file
        subprocess.call(["losetup", "/dev/loop%s" % os.getpid(), options.image_file])
        # Launch qemu
        cmdline = ["xl", "create"]
        if not options.detach:
            cmdline += ["-c"]
        cmdline += [xenfile.name]
        if options.dry_run:
            print(format_args(cmdline))
        else:
            subprocess.call(cmdline)
    except:
        pass
    finally:
        xenfile.close()
        #delete loop device
        subprocess.call(["losetup", "-d", "/dev/loop%s" % os.getpid()])
        cleanups()

def start_osv_vmware(options):
    args = [
        '#!/usr/bin/vmware',
        '.encoding = "UTF-8"',
        'config.version = "8"',
        'virtualHW.version = "8"',
        'scsi0.present = "TRUE"',
        'scsi0.virtualDev = "pvscsi"',
        'scsi0:0.fileName = "osv.vmdk"',
        'ethernet0.present = "TRUE"',
        'ethernet0.connectionType = "nat"',
        'ethernet0.virtualDev = "vmxnet3"',
        'ethernet0.addressType = "generated"',
        'pciBridge0.present = "TRUE"',
        'pciBridge4.present = "TRUE"',
        'pciBridge4.virtualDev = "pcieRootPort"',
        'pciBridge4.functions = "8"',
        'hpet0.present = "TRUE"',
        'guestOS = "ubuntu-64"',
        'scsi0:0.present = "TRUE"',
        'floppy0.present = "FALSE"',
        'serial0.present = "TRUE"',
        'serial0.fileType = "network"',
        'serial0.fileName = "telnet://127.0.0.1:10000"',
        'debugStub.listen.guest64 = "TRUE"',
        'debugStub.listen.guest64.remote = "TRUE"',
    ]
    try:
        memory = int(options.memsize)
    except ValueError:
        memory = options.memsize

        if memory[-1:].upper() == "M":
            memory = int(memory[:-1])
        elif memory[-2:].upper() == "MB":
            memory = int(memory[:-2])
        elif memory[-1:].upper() == "G":
            memory = 1024 * int(memory[:-1])
        elif memory[-2:].upper() == "GB":
            memory = 1024 * int(memory[:-2])
        else:
            print("Unrecognized memory size", file=sys.stderr)
            return

    args += [
        'memsize = "%d"' % (memory),
        'numvcpus = "%s"' % (options.vcpus),
        'displayName = "osv-%d"' % (os.getpid()),
    ]

    vmxfile = open("build/%s/osv.vmx" % options.opt_path, "w")
    vmxfile.writelines("%s\n" % item for item in args)
    vmxfile.flush()

    try:
        # Convert disk image to vmdk
        subprocess.call(["qemu-img", "convert", "-O", "vmdk", options.image_file, "build/%s/osv.vmdk" % options.opt_path])
        # Launch vmware
        cmdline = ["vmrun", "start", vmxfile.name]
        if options.graphics:
            cmdline += ["gui"]
        else:
            cmdline += ["nogui"]
        if options.dry_run:
            print(format_args(cmdline))
        else:
            subprocess.call(cmdline)
        # Connect serial console via TCP
        subprocess.call(["telnet", "127.0.0.1", "10000"])
    except:
        pass
    finally:
        vmxfile.close()
        cleanups()

def start_osv(options):
    launchers = {
            "xen" : start_osv_xen,
            "xenpv" : start_osv_xen,
            "none" : start_osv_qemu,
            "qemu" : start_osv_qemu,
            "kvm" : start_osv_qemu,
            "vmware" : start_osv_vmware,
    }
    try:
        launchers[options.hypervisor](options)
    except KeyError:
        print("Unrecognized hypervisor selected", file=sys.stderr)
        return

def choose_hypervisor(external_networking):
    if os.path.exists('/dev/kvm'):
        return 'kvm'
    if (os.path.exists('/proc/xen/capabilities')
        and 'control_d' in file('/proc/xen/capabilities').read()
        and external_networking):
        return 'xen'
    return 'qemu'

def main(options):
    set_imgargs(options)
    start_osv(options)

if __name__ == "__main__":
    # Parse arguments
    parser = argparse.ArgumentParser(prog='run')
    parser.add_argument("-d", "--debug", action="store_true",
                        help="start debug version")
    parser.add_argument("-r", "--release", action="store_true",
                        help="start release version")
    parser.add_argument("-w", "--wait", action="store_true",
                        help="don't start OSv till otherwise specified, e.g. through the QEMU monitor or a remote gdb")
    parser.add_argument("-i", "--image", action="store", default=None, metavar="IMAGE",
                        help="path to disk image file. defaults to build/$mode/usr.img")
    parser.add_argument("-S", "--scsi", action="store_true", default=False,
                        help="use virtio-scsi instead of virtio-blk")
    parser.add_argument("-A", "--sata", action="store_true", default=False,
                        help="use AHCI instead of virtio-blk")
    parser.add_argument("-I", "--ide", action="store_true", default=False,
                        help="use ide instead of virtio-blk")
    parser.add_argument("-3", "--vmxnet3", action="store_true", default=False,
                        help="use vmxnet3 instead of virtio-net")
    parser.add_argument("-n", "--networking", action="store_true",
                        help="needs root. tap networking, specify interface")
    parser.add_argument("-b", "--bridge", action="store", default="virbr0",
                        help="bridge name for tap networking")
    parser.add_argument("-v", "--vhost", action="store_true",
                        help="needs root. tap networking and vhost")
    parser.add_argument("-m", "--memsize", action="store", default="2G",
                        help="specify memory: ex. 1G, 2G, ...")
    parser.add_argument("-c", "--vcpus", action="store", default="4",
                        help="specify number of vcpus")
    parser.add_argument("-e", "--execute", action="store", default=None, metavar="CMD",
                        help="edit command line before execution")
    parser.add_argument("-p", "--hypervisor", action="store", default="auto",
                        help="choose hypervisor to run: kvm, xen, xenpv, vmware, none (plain qemu)")
    parser.add_argument("-D", "--detach", action="store_true",
                        help="run in background, do not connect the console")
    parser.add_argument("-H", "--no-shutdown", action="store_true",
                        help="don't restart qemu automatically (allow debugger to connect on early errors)")
    parser.add_argument("-s", "--with-signals", action="store_true", default=False,
                        help="qemu only. handle signals instead of passing keys to the guest. pressing ctrl+c from console will kill the emulator")
    parser.add_argument("-u", "--unsafe-cache", action="store_true",
                        help="Set cache to unsafe. Use it at your own risk.")
    parser.add_argument("-g", "--graphics", action="store_true",
                        help="Enable graphics mode.")
    parser.add_argument("-V", "--verbose", action="store_true",
                        help="pass --verbose to OSv, to display more debugging information on the console")
    parser.add_argument("--forward", metavar="RULE", action="append", default=[],
                        help="add network forwarding RULE (QEMU syntax)")
    parser.add_argument("--dry-run", action="store_true",
                        help="do not run, just print the command line")
    parser.add_argument("--jvm-debug", action="store_true",
                        help="start JVM with a debugger server")
    parser.add_argument("--jvm-suspend", action="store_true",
                        help="start JVM with a suspended debugger server")
    parser.add_argument("--mac", action="store",
                        help="set MAC address for NIC")
    parser.add_argument("--vnc", action="store", default=":1",
                        help="specify vnc port number")
    parser.add_argument("--api", action="store_true",
                        help="redirect the API port (8000) for user-mode networking")
    parser.add_argument("--pass-args", action="append",
                        help="pass arguments to underlying hypervisor (e.g. qemu)")
    parser.add_argument("--trace", default=[], action='append',
                        help="enable tracepoints")
    parser.add_argument("--trace-backtrace", action="store_true",
                        help="enable collecting of backtrace at tracepoints")
    parser.add_argument("--sampler", action="store", nargs='?', const='1000',
                        help="start sampling profiler. optionally specify sampling frequency in Hz")
    parser.add_argument("--qemu-path", action="store",
                        default="qemu-system-x86_64",
                        help="specify qemu command path")
    parser.add_argument("--nics", action="store", default="1",
                        help="number of NICs configured for the VM")
    parser.add_argument("--novnc", action="store_true",
                        help="disable vnc")
    parser.add_argument("--nogdb", action="store_true",
                        help="disable gdb")
    parser.add_argument("--gdb", action="store", default="1234",
                        help="specify gdb port number")
    parser.add_argument("--script", action="store",
                        help="XEN define configuration script for vif")
    cmdargs = parser.parse_args()
    cmdargs.opt_path = "debug" if cmdargs.debug else "release" if cmdargs.release else "last"
    cmdargs.image_file = os.path.abspath(cmdargs.image or "build/%s/usr.img" % cmdargs.opt_path)
    if not os.path.exists(cmdargs.image_file):
        raise Exception('Image file %s does not exist.' % cmdargs.image_file)

    if cmdargs.hypervisor == "auto":
        cmdargs.hypervisor = choose_hypervisor(cmdargs.networking)
    # Call main
    main(cmdargs)
