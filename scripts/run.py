#!/usr/bin/env python

import subprocess
import sys
import argparse

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
        "-device", "virtio-net-pci",
        "-drive", ("file=build/%s/loader.img,if=virtio,cache=unsafe" % opt_path),
        "-drive", ("file=build/%s/usr.img,if=virtio,cache=unsafe" % opt_path)]

    subprocess.call(["qemu-system-x86_64"] + args)

def main():
    set_imgargs()
    start_osv()

if (__name__ == "__main__"):
    # Parse arguments
    parser = argparse.ArgumentParser(prog='run')
    parser.add_argument("-d", "--debug", action="store_true", 
                        help="start debug version")
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
    