#!/usr/bin/env python
import subprocess
import sys
import argparse

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
    
    args = ["setargs", "build/%s/loader.img" % opt_path, cmdargs.execute]
    subprocess.call(["scripts/imgedit.py"] + args)
    
def start_osv():
    args = [
        "-vnc", ":1",
        "-enable-kvm",
        "-gdb", "tcp::1234,server,nowait",
        "-cpu", "host,+x2apic",
        "-m", cmdargs.memsize,
        "-smp", cmdargs.vcpus,
        "-chardev", "stdio,mux=on,id=stdio",
        "-mon", "chardev=stdio,mode=readline,default",
        "-device", "isa-serial,chardev=stdio",
        "-drive", ("file=build/%s/loader.img,if=virtio,cache=unsafe" % opt_path),
        "-drive", ("file=build/%s/usr.img,if=virtio,cache=unsafe" % opt_path)]
    
    if (cmdargs.networking):
        args += ["-netdev", "bridge,id=hn0,br=virbr0,helper=/usr/libexec/qemu-bridge-helper"]
        args += ["-device", "virtio-net-pci,netdev=hn0,id=nic1"]
    else:
        args += ["-netdev", "user,id=un0,net=192.168.122.0/24,host=192.168.122.1"]
        args += ["-device", "virtio-net-pci,netdev=un0"]
        
    try:
        # Save the current settings of the stty
        stty_save()

        # Launch qemu
        subprocess.call(["qemu-system-x86_64"] + args)
    except:
        pass
    finally:
        cleanups()

def main():
    set_imgargs()
    start_osv()

if (__name__ == "__main__"):
    # Parse arguments
    parser = argparse.ArgumentParser(prog='run')
    parser.add_argument("-d", "--debug", action="store_true", 
                        help="start debug version")
    parser.add_argument("-n", "--networking", action="store_true",
                        help="needs root. tap networking, specify interface")
    parser.add_argument("-m", "--memsize", action="store", default="1G",
                        help="specify memory: ex. 1G, 2G, ...")
    parser.add_argument("-c", "--vcpus", action="store", default="4",
                        help="specify number of vcpus")
    parser.add_argument("-e", "--execute", action="store", default=None, metavar="CMD",
                        help="edit command line before execution")
    cmdargs = parser.parse_args()
    opt_path = "debug" if cmdargs.debug else "release"
    
    # Call main
    main()
    
