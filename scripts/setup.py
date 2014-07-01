#!/usr/bin/python2

# set up a development environment for OSv.  Run as root.

import sys, platform
import os, os.path, distutils.version
import subprocess, StringIO

class Fedora(object):
    name = 'Fedora'
    install = 'yum -y install'
    packages = ['gcc-c++', 'gcc-c++-aarch64-linux-gnu', 'git', 'gdb', 'qemu-img',
                'qemu-system-x86', 'libvirt', 'maven', 'java-1.7.0-openjdk',
                'ant', 'autoconf', 'automake', 'boost-static', 'genromfs', 'libtool',
                'flex', 'bison', 'maven-shade-plugin', 'python-dpkt', 'tcpdump', 'gdb'
                ]
    class Fedora_20(object):
        packages = []
        version = '20'
    versions = [Fedora_20]

distros = [
           Fedora(),
           ]

(name, version, id) = platform.linux_distribution()

for distro in distros:
    if distro.name == name:
        for dver in distro.versions:
            if dver.version == version:
                pkg = distro.packages + dver.packages
                subprocess.check_call(distro.install + ' ' + str.join(' ', pkg), shell = True)
                sys.exit(0)
        print 'Your distribution version is not supported by this script'
        sys.exit(1)

print 'Your distribution is not supported by this script.'
sys.exit(2)

