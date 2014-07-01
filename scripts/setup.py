#!/usr/bin/python2

# set up a development environment for OSv.  Run as root.

import sys
import os, os.path, distutils.version
import subprocess, StringIO

class Fedora(object):
    detect = '/etc/fedora-release'
    get_version = 'rpm -q --qf %{version} fedora-release'
    install = 'yum -y install'
    packages = ['gcc-c++', 'gcc-c++-aarch64-linux-gnu', 'git', 'gdb', 'qemu-img',
                'qemu-system-x86', 'libvirt', 'maven', 'java-1.7.0-openjdk',
                ]
    class Fedora_20(object):
        packages = []
        version = '20'
    versions = [Fedora_20]

distros = [
           Fedora(),
           ]

for distro in distros:
    if os.path.exists(distro.detect):
        version = distutils.version.LooseVersion(
                         subprocess.check_output(distro.get_version, shell = True))
        for dver in distro.versions:
            if version == distutils.version.LooseVersion(dver.version):
                pkg = distro.packages + dver.packages
                subprocess.check_call(distro.install + ' ' + str.join(' ', pkg), shell = True)
                sys.exit(0)
        print 'Your distribution version is not supported by this script'
        sys.exit(1)

print 'Your distribution is not supported by this script.'
sys.exit(2)

