#!/usr/bin/python2

# set up a development environment for OSv.  Run as root.

import sys, platform, argparse
import os, os.path, distutils.version
import subprocess, StringIO

standard_ec2_packages=['python-pip', 'wget']
standard_ec2_post_install = ['pip install awscli &&'
                             'wget http://s3.amazonaws.com/ec2-downloads/ec2-api-tools.zip &&'
                             'rm -rf /usr/local/ec2 &&'
                             'mkdir /usr/local/ec2 &&'
                             'unzip ec2-api-tools.zip -d /usr/local/ec2 &&'
                             'rm -f ec2-api-tools.zip &&'
                             'cat /etc/environment | grep -v "^EC2_HOME=" | grep -v "^JAVA_HOME" | grep -v "PATH=\$EC2_HOME" | cat > /etc/environment_temp &&'
                             'echo "EC2_HOME=`ls -d /usr/local/ec2/ec2-api-tools-*`" >> /etc/environment_temp &&'
                             'echo "JAVA_HOME=`readlink -f /usr/bin/javac | sed \"s:bin/javac::\"`" >> /etc/environment_temp &&'
                             'echo "PATH=\$EC2_HOME/bin:\$PATH" >> /etc/environment_temp &&'
                             'cp /etc/environment /etc/environment.bk &&'
                             'mv /etc/environment_temp /etc/environment &&'
                             'echo Done. Re-login to apply environment changes for EC2']

class Fedora(object):
    name = 'Fedora'
    install = 'yum -y install'
    packages = ['gcc-c++', 'gcc-c++-aarch64-linux-gnu', 'git', 'gdb', 'qemu-img',
                'qemu-system-x86', 'libvirt', 'maven', 'java-1.7.0-openjdk',
                'ant', 'autoconf', 'automake', 'boost-static', 'genromfs', 'libtool',
                'flex', 'bison', 'maven-shade-plugin', 'python-dpkt', 'tcpdump', 'gdb',
                'gnutls-utils', 'openssl', 'python-requests'
                ]
    ec2_packages = standard_ec2_packages
    test_packages = ['openssl-devel']
    ec2_post_install = standard_ec2_post_install

    class Fedora_20(object):
        packages = []
        ec2_packages = []
        test_packages = []
        ec2_post_install = None
        version = '20'
    versions = [Fedora_20]

class Ubuntu(object):
    name = 'Ubuntu'
    install = 'apt-get -y install'
    packages = ['build-essential', 'libboost-all-dev', 'genromfs', 'autoconf',
                'libtool', 'openjdk-7-jdk', 'ant', 'qemu-utils', 'maven',
                'libmaven-shade-plugin-java', 'python-dpkt', 'tcpdump', 'gdb', 'qemu-system-x86',
                'gawk', 'gnutls-bin', 'openssl', 'python-requests'
                ]
    ec2_packages = standard_ec2_packages
    test_packages = ['libssl-dev', 'zip']
    ec2_post_install = None

    class Ubuntu_14_04(object):
        packages = []
        ec2_packages = ['ec2-api-tools', 'awscli']
        test_packages = []
        ec2_post_install = None
        version = '14.04'

    class Ubuntu_13_10(object):
        packages = []
        ec2_packages = []
        test_packages = []
        ec2_post_install = standard_ec2_post_install
        version = '13.10'

    versions = [Ubuntu_14_04, Ubuntu_13_10]

distros = [
           Fedora(),
           Ubuntu()
           ]

parser = argparse.ArgumentParser(prog='setup')
parser.add_argument("-e", "--ec2", action="store_true",
                    help="install packages required by EC2 release and test scripts")
parser.add_argument("-t", "--test", action="store_true",
                    help="install packages required by testing tools")
cmdargs = parser.parse_args()

(name, version, id) = platform.linux_distribution()

for distro in distros:
    if distro.name == name:
        for dver in distro.versions:
            if dver.version == version:
                pkg = distro.packages + dver.packages
                if cmdargs.ec2:
                    pkg += distro.ec2_packages + dver.ec2_packages
                if cmdargs.test:
                    pkg += distro.test_packages + dver.test_packages
                subprocess.check_call(distro.install + ' ' + str.join(' ', pkg), shell = True)
                if cmdargs.ec2:
                    if distro.ec2_post_install:
                        subprocess.check_call(distro.ec2_post_install, shell = True)
                    if dver.ec2_post_install:
                        subprocess.check_call(dver.ec2_post_install, shell = True)
                sys.exit(0)
        print 'Your distribution version is not supported by this script'
        sys.exit(1)

print 'Your distribution is not supported by this script.'
sys.exit(2)

