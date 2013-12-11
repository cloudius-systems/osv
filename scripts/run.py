#!/usr/bin/env python2
import subprocess
import sys
import argparse
import os
import tempfile
import errno

stty_params=None

devnull = open('/dev/null', 'w')

def stty_save():
    global stty_params
    p = subprocess.Popen(["stty", "-g"], stdout=subprocess.PIPE, stderr=devnull)
    stty_params, err = p.communicate()
    stty_params = stty_params.strip()
    
def stty_restore():
    if (stty_params):
        subprocess.call(["stty", stty_params], stderr=devnull)

def cleanups():
    "cleanups after execution"
    stty_restore()

def set_imgargs():
    if (not cmdargs.execute):
        return
    
    args = ["setargs", image_file, cmdargs.execute]
    subprocess.call(["scripts/imgedit.py"] + args)

def is_direct_io_supported(path):
    if not os.path.exists(path):
        raise Exception('Path not found: ' + path)

    try:
        file = os.open(path, os.O_RDONLY | os.O_DIRECT)
        os.close(file)
        return True
    except OSError, e:
        if e.errno == errno.EINVAL:
            return False;
        raise
    
def start_osv_qemu():
    if (cmdargs.unsafe_cache):
        cache = 'unsafe'
    else:
        cache = 'none' if is_direct_io_supported(image_file) else 'unsafe'

    args = [
        "-vnc", ":1",
        "-gdb", "tcp::1234,server,nowait",
        "-m", cmdargs.memsize,
        "-smp", cmdargs.vcpus,
        "-drive", "file=%s,if=virtio,cache=%s" % (image_file, cache)]
    
    if (cmdargs.no_shutdown):
        args += ["-no-reboot", "-no-shutdown"]

    if (cmdargs.wait):
		args += ["-S"]
     
    if (cmdargs.networking):
        if (cmdargs.vhost):
            args += ["-netdev", "tap,id=hn0,script=scripts/qemu-ifup.sh,vhost=on"]
            args += ["-device", "virtio-net-pci,netdev=hn0,id=nic1"]
        else:
            args += ["-netdev", "bridge,id=hn0,br=%s,helper=/usr/libexec/qemu-bridge-helper" % (cmdargs.bridge)]
            args += ["-device", "virtio-net-pci,netdev=hn0,id=nic1"]
    else:
        args += ["-netdev", "user,id=un0,net=192.168.122.0/24,host=192.168.122.1"]
        args += ["-device", "virtio-net-pci,netdev=un0"]
        args += ["-redir", "tcp:8080::8080"]
        args += ["-redir", "tcp:2222::22"]
        for rule in cmdargs.forward:
            args += ['-redir', rule]
        
    args += ["-device", "virtio-rng-pci"]

    if cmdargs.hypervisor == "kvm":
        args += ["-enable-kvm", "-cpu", "host,+x2apic"]
    elif (cmdargs.hypervisor == "none") or (cmdargs.hypervisor == "qemu"):
        pass

    if (cmdargs.detach):
        args += ["-daemonize"]
    else:
        signal_option = ('off', 'on')[cmdargs.with_signals]
        args += ["-chardev", "stdio,mux=on,id=stdio,signal=%s" % signal_option]
        args += ["-mon", "chardev=stdio,mode=readline,default"]
        args += ["-device", "isa-serial,chardev=stdio"]

    try:
        # Save the current settings of the stty
        stty_save()

        # Launch qemu
        qemu_env = os.environ.copy()
        qemu_env['OSV_BRIDGE'] = cmdargs.bridge
        subprocess.call(["qemu-system-x86_64"] + args, env = qemu_env)
    except OSError, e:
        if e.errno == errno.ENOENT:
          print("'qemu-system-x86_64' binary not found. Please install the qemu-system-x86 package.")
        else:
          print("OS error({0}): \"{1}\" while running qemu-system-x86_64 {2}".
                format(e.errno, e.strerror, " ".join(args)))
    finally:
        cleanups()

def start_osv_xen():
    if cmdargs.hypervisor == "xen":
        args = [
            "builder='hvm'",
            "xen_platform_pci=1",
            "acpi=1",
            "apic=1",
            "boot='c'",
        ]
    else:
        args = [ "kernel='%s/build/%s/loader.elf'" % (os.getcwd(), opt_path) ]

    try:
        memory = int(cmdargs.memsize)
    except ValueError:
        memory = cmdargs.memsize

        if memory[-1:].upper() == "M":
            memory = int(memory[:-1])
        elif memory[-2:].upper() == "MB":
            memory = int(memory[:-2])
        elif memory[-1:].upper() == "G":
            memory = 1024 * int(memory[:-1])
        elif memory[-2:].upper() == "GB":
            memory = memory[:-2]
            memory = 1024 * int(memory[:-2])
        else:
            print >> sys.stderr, "Unrecognized memory size"
            return;

    args += [
        "memory=%d" % (memory),
        "vcpus=%s" % (cmdargs.vcpus),
        "maxcpus=%s" % (cmdargs.vcpus),
        "name='osv-%d'" % (os.getpid()),
        "disk=['%s,qcow2,hda,rw']" % image_file,
        "serial='pty'",
        "paused=0",
        "on_crash='preserve'"
    ]

    if cmdargs.networking:
        args += [ "vif=['bridge=%s']" % (cmdargs.bridge)]

    # Using xm would allow us to get away with creating the file, but it comes
    # with its set of problems as well. Stick to xl.
    xenfile = tempfile.NamedTemporaryFile(mode="w")
    xenfile.writelines( "%s\n" % item for item in args )
    xenfile.flush()

    try:
        # Save the current settings of the stty
        stty_save()

        # Launch qemu
        cmdline = ["xl", "create" ]
        if not cmdargs.detach:
            cmdline += [ "-c" ]
        cmdline += [ xenfile.name ]
        subprocess.call(cmdline)
    except:
        pass
    finally:
        xenfile.close()
        cleanups()

def start_osv():
    launchers = {
            "xen" : start_osv_xen,
            "xenpv" : start_osv_xen,
            "none" : start_osv_qemu,
            "qemu" : start_osv_qemu,
            "kvm" : start_osv_qemu,
    }
    try:
        launchers[cmdargs.hypervisor]()
    except KeyError: 
        print >> sys.stderr, "Unrecognized hypervisor selected"
        return;

def choose_hypervisor(external_networking):
    if os.path.exists('/dev/kvm'):
        return 'kvm'
    if (os.path.exists('/proc/xen/capabilities')
        and 'control_d' in file('/proc/xen/capabilities').read()
        and external_networking):
        return 'xen'
    return 'qemu'

def main():
    set_imgargs()
    start_osv()

if (__name__ == "__main__"):
    # Parse arguments
    parser = argparse.ArgumentParser(prog='run')
    parser.add_argument("-d", "--debug", action="store_true", 
                        help="start debug version")
    parser.add_argument("-w", "--wait", action="store_true",
                        help="don't start OSv till otherwise specified, e.g. through the QEMU monitor or a remote gdb")
    parser.add_argument("-i", "--image", action="store", default=None, metavar="IMAGE",
                        help="path to disk image file. defaults to build/$mode/usr.img")
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
                        help="choose hypervisor to run: kvm, xen, xenpv, none (plain qemu)")
    parser.add_argument("-D", "--detach", action="store_true",
                        help="run in background, do not connect the console")
    parser.add_argument("-H", "--no-shutdown", action="store_true",
                        help="don't restart qemu automatically (allow debugger to connect on early errors)")
    parser.add_argument("-s", "--with-signals", action="store_true", default=False,
                        help="qemu only. handle signals instead of passing keys to the guest. pressing ctrl+c from console will kill the emulator")
    parser.add_argument("-u", "--unsafe-cache", action="store_true",
                        help="Set cache to unsafe. Use it at your own risk.")

    parser.add_argument("--forward", metavar = "RULE", action = "append", default = [],
                        help = "add network forwarding RULE (QEMU syntax)")
    cmdargs = parser.parse_args()
    opt_path = "debug" if cmdargs.debug else "release"
    image_file = os.path.abspath(cmdargs.image or "build/%s/usr.img" % opt_path)
    
    if(cmdargs.hypervisor == "auto"):
        cmdargs.hypervisor = choose_hypervisor(cmdargs.networking);
    # Call main
    main()
    
