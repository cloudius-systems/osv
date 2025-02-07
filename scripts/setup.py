#!/usr/bin/python3

# set up a development environment for OSv.  Run as root.

import sys, argparse
import subprocess, os
from linux_distro import linux_distribution

arch = os.uname().machine
if arch == 'x86_64':
   qemu_extention = 'x86'
else:
   qemu_extention = 'arm'

standard_ec2_packages = ['python-pip', 'wget']
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
    pre_install = '(yum list installed compat-openssl10-devel 2>/dev/null && yum -y remove compat-openssl10-devel) || echo "package compat-openssl10-devel not found -> no need to remove it"'
    install = 'yum -y install --allowerasing --forcearch ' + arch
    packages = [
                'ant',
                'autoconf',
                'automake',
                'binutils',
                'bison',
                'boost-static',
                'curl',
                'flex',
                'gcc-c++',
                'gdb',
                'genromfs',
                'git',
                'gnutls-utils',
                'grep',
                'libedit-devel',
                'libstdc++-static',
                'libtool',
                'libvirt',
                'make',
                'maven',
                'maven-shade-plugin',
                'ncurses',
                'ncurses-devel',
                'openssl',
                'openssl-libs',
                'openssl-devel',
                'p11-kit',
                'patch',
                'python3-dpkt',
                'python3-requests',
                'qemu-img',
                'qemu-system-%s' % qemu_extention,
                'tcpdump',
                'unzip',
                'wget',
                'yaml-cpp-devel',
                'pax-utils',
                'java-1.8.0-openjdk',
                'lua',
                'lua-devel',
                'glibc-static',
                 ]
    if arch == 'x86_64':
        packages = packages + [ 'gcc-c++-aarch64-linux-gnu' ]

    ec2_packages = standard_ec2_packages
    test_packages = ['openssl-devel']
    ec2_post_install = standard_ec2_post_install

    class Fedora_27(object):
        packages = []
        ec2_packages = []
        test_packages = []
        ec2_post_install = None
        version = '27'

    class Fedora_28(object):
        packages = []
        ec2_packages = []
        test_packages = []
        ec2_post_install = None
        version = '28'

    class Fedora_29(object):
        packages = []
        ec2_packages = []
        test_packages = []
        ec2_post_install = None
        version = '29'

    class Fedora_30(object):
        packages = []
        ec2_packages = []
        test_packages = []
        ec2_post_install = None
        version = '30'

    class Fedora_31(object):
        packages = []
        ec2_packages = []
        test_packages = []
        ec2_post_install = None
        version = '31'

    class Fedora_32(object):
        packages = []
        ec2_packages = []
        test_packages = []
        ec2_post_install = None
        version = '32'

    class Fedora_33(object):
        packages = []
        ec2_packages = []
        test_packages = []
        ec2_post_install = None
        version = '33'

    class Fedora_34(object):
        packages = []
        ec2_packages = []
        test_packages = []
        ec2_post_install = None
        version = '34'

    class Fedora_35(object):
        packages = []
        ec2_packages = []
        test_packages = []
        ec2_post_install = None
        version = '35'

    class Fedora_37(object):
        packages = []
        ec2_packages = []
        test_packages = []
        ec2_post_install = None
        version = '37'

    class Fedora_38(object):
        packages = []
        ec2_packages = []
        test_packages = []
        ec2_post_install = None
        version = '38'

    class Fedora_39(object):
        packages = []
        ec2_packages = []
        test_packages = []
        ec2_post_install = None
        version = '39'

    class Fedora_40(object):
        packages = []
        ec2_packages = []
        test_packages = []
        ec2_post_install = None
        version = '40'

    class Fedora_41(object):
        packages = []
        ec2_packages = []
        test_packages = []
        ec2_post_install = None
        version = '41'

    versions = [
        Fedora_27, Fedora_28, Fedora_29, Fedora_30, Fedora_31, Fedora_32, Fedora_33, Fedora_34, Fedora_35, Fedora_37, Fedora_38, Fedora_39, Fedora_40, Fedora_41
    ]


class RHELbased(Fedora):
    name = ['Scientific Linux', 'NauLinux', 'Red Hat Enterprise Linux', 'Oracle Linux']

    class RHELbased_70(object):
        packages = []
        ec2_packages = []
        test_packages = []
        ec2_post_install = None
        version = '7.0'

    class RHELbased_71(object):
        packages = []
        ec2_packages = []
        test_packages = []
        ec2_post_install = None
        version = '7.1'

    class RHELbased_72(object):
        packages = []
        ec2_packages = []
        test_packages = []
        ec2_post_install = None
        version = '7.2'

    class RHELbased_73(object):
        packages = []
        ec2_packages = []
        test_packages = []
        ec2_post_install = None
        version = '7.3'

    versions = [RHELbased_70, RHELbased_71, RHELbased_72, RHELbased_73]

class Debian(object):
    name = 'debian'
    install = 'apt-get -y install'
    packages = [
                'ant',
                'autoconf',
                'automake',
                'binutils',
                'bison',
                'build-essential',
                'curl',
                'flex',
                'gawk',
                'gdb',
                'genromfs',
                'git',
                'gnutls-bin',
                'grep',
                'libboost-all-dev',
                'libedit-dev',
                'libmaven-shade-plugin-java',
                'libncurses5-dev',
                'libssl-dev',
                'libtool',
                'libyaml-cpp-dev',
                'make',
                'maven',
                'openssl',
                'p11-kit',
                'python-dpkt',
                'python-requests',
                'qemu-system-%s' % qemu_extention,
                'qemu-utils',
                'tcpdump',
                'unzip',
                'wget',
                ]

    ec2_packages = standard_ec2_packages
    test_packages = ['libssl-dev', 'zip']
    ec2_post_install = None

    class debian(object):
        packages = ['lib32stdc++-4.9-dev', 'openjdk-7-jdk',]
        ec2_packages = []
        test_packages = []
        ec2_post_install = None
        version = 'jessie/sid'

    class Debian_9_3(object):
        packages = ['lib32stdc++-6-dev', 'openjdk-8-jdk',]
        ec2_packages = []
        test_packages = []
        ec2_post_install = None
        version = '9.3'

    versions = [debian, Debian_9_3]

class Ubuntu(object):
    name = 'Ubuntu'
    install = 'apt-get -y install'
    packages = [
                'ant',
                'autoconf',
                'automake',
                'binutils',
                'bison',
                'build-essential',
                'curl',
                'flex',
                'gawk',
                'gdb',
                'genromfs',
                'git',
                'gnutls-bin',
                'grep',
                'libboost-all-dev',
                'libedit-dev',
                'libmaven-shade-plugin-java',
                'libncurses5-dev',
                'libssl-dev',
                'libtool',
                'libyaml-cpp-dev',
                'make',
                'maven',
                'openssl',
                'p11-kit',
                'python3-requests',
                'qemu-system-%s' % qemu_extention,
                'qemu-utils',
                'tcpdump',
                'unzip',
                'wget',
                'lua5.3',
                'liblua5.3',
                'pax-utils',
                'openjdk-8-jdk',
                ]
    if arch == 'x86_64':
        packages = packages + [ 'g++-aarch64-linux-gnu', 'gdb-multiarch' ]

    ec2_packages = standard_ec2_packages
    test_packages = ['libssl-dev', 'zip']
    ec2_post_install = None

    class Ubuntu_24_04(object):
        packages = ['bridge-utils', 'libvirt-daemon-system', 'libvirt-clients', 'python3-dpkt']
        ec2_packages = ['ec2-api-tools', 'awscli']
        test_packages = []
        ec2_post_install = None
        version = '24.04'

    class Ubuntu_22_04(object):
        packages = ['bridge-utils', 'libvirt-daemon-system', 'libvirt-clients', 'python3-dpkt']
        ec2_packages = ['ec2-api-tools', 'awscli']
        test_packages = []
        ec2_post_install = None
        version = '22.04'

    class Ubuntu_21_10(object):
        packages = ['bridge-utils', 'libvirt-daemon-system', 'libvirt-clients', 'python3-dpkt']
        ec2_packages = ['ec2-api-tools', 'awscli']
        test_packages = []
        ec2_post_install = None
        version = '21.10'

    class Ubuntu_21_04(object):
        packages = ['bridge-utils', 'libvirt-daemon-system', 'libvirt-clients', 'python3-dpkt']
        ec2_packages = ['ec2-api-tools', 'awscli']
        test_packages = []
        ec2_post_install = None
        version = '21.04'

    class Ubuntu_20_10(object):
        packages = ['bridge-utils', 'libvirt-daemon-system', 'libvirt-clients', 'python3-dpkt']
        ec2_packages = ['ec2-api-tools', 'awscli']
        test_packages = []
        ec2_post_install = None
        version = '20.10'

    class Ubuntu_20_04(object):
        packages = ['bridge-utils', 'libvirt-daemon-system', 'libvirt-clients', 'python3-dpkt']
        ec2_packages = ['ec2-api-tools', 'awscli']
        test_packages = []
        ec2_post_install = None
        version = '20.04'

    class Ubuntu_19_10(object):
        packages = ['bridge-utils', 'libvirt-daemon-system', 'libvirt-clients', 'python3-dpkt']
        ec2_packages = ['ec2-api-tools', 'awscli']
        test_packages = []
        ec2_post_install = None
        version = '19.10'

    class Ubuntu_19_04(object):
        packages = ['bridge-utils', 'libvirt-daemon-system', 'libvirt-clients', 'python-dpkt']
        ec2_packages = ['ec2-api-tools', 'awscli']
        test_packages = []
        ec2_post_install = None
        version = '19.04'

    class Ubuntu_18_10(object):
        packages = ['bridge-utils', 'libvirt-daemon-system', 'libvirt-clients', 'python-dpkt']
        ec2_packages = ['ec2-api-tools', 'awscli']
        test_packages = []
        ec2_post_install = None
        version = '18.10'

    class Ubuntu_18_04(object):
        packages = ['bridge-utils', 'libvirt-bin', 'python-dpkt']
        ec2_packages = ['ec2-api-tools', 'awscli']
        test_packages = []
        ec2_post_install = None
        version = '18.04'

    class Ubuntu_17_04(object):
        packages = ['libvirt-bin', 'python-dpkt']
        ec2_packages = ['ec2-api-tools', 'awscli']
        test_packages = []
        ec2_post_install = None
        version = '17.04'

    class Ubuntu_16_04(object):
        packages = ['libvirt-bin', 'python-dpkt']
        ec2_packages = ['ec2-api-tools', 'awscli']
        test_packages = []
        ec2_post_install = None
        version = '16.04'

    versions = [Ubuntu_24_04, Ubuntu_22_04, Ubuntu_21_10, Ubuntu_21_04, Ubuntu_20_10, Ubuntu_20_04, Ubuntu_19_10, Ubuntu_19_04, Ubuntu_18_10, Ubuntu_18_04, Ubuntu_17_04, Ubuntu_16_04]

class LinuxMint(Ubuntu):
    name = 'LinuxMint'

    class LinuxMint_18_03(object):
        packages = ['openjdk-8-jdk']
        ec2_packages = ['ec2-api-tools', 'awscli']
        test_packages = []
        ec2_post_install = None
        version = '18.3'

    class LinuxMint_19(object):
        packages = ['openjdk-8-jdk', 'bridge-utils']
        ec2_packages = ['ec2-api-tools', 'awscli']
        test_packages = []
        ec2_post_install = None
        version = '19'

    versions = [LinuxMint_18_03, LinuxMint_19]

class CentOS(object):
    name = 'CentOS Linux'
    install = 'yum -y install'
    packages = [
                'ant',
                'autoconf',
                'automake',
                'binutils',
                'curl',
                'git',
                'grep',
                'gnutls-utils',
                'libtool',
                'maven',
                'maven-shade-plugin',
                'openssl',
                'openssl-libs',
                'openssl-devel',
                'p11-kit',
                'patch',
                'python3-requests',
                'qemu-img',
                'tcpdump',
                'unzip',
                'wget',
                'yaml-cpp-devel',
                'pax-utils',
                'java-1.8.0-openjdk',
                'devtoolset-9-toolchain',
                 ]

    test_packages = ['openssl-devel']

    class CentOS_7(object):
        pre_install = 'yum -y install epel-release centos-release-scl centos-release-scl-rh https://packages.endpoint.com/rhel/7/os/x86_64/endpoint-repo-1.7-1.x86_64.rpm'
        packages = []
        ec2_packages = []
        test_packages = []
        ec2_post_install = None
        post_install = 'echo "---> Run \'scl enable devtoolset-9 bash\' or add \'source /opt/rh/devtoolset-9/enable\' to ~/.bashrc or ~/.bash_profile" to enable GCC 9 before building OSv !'
        version = '7'

    versions = [CentOS_7]

distros = [
           Debian(),
           Fedora(),
           Ubuntu(),
           LinuxMint(),
           RHELbased(),
           CentOS()
           ]

parser = argparse.ArgumentParser(prog='setup')
parser.add_argument("-e", "--ec2", action="store_true",
                    help="install packages required by EC2 release and test scripts")
parser.add_argument("-t", "--test", action="store_true",
                    help="install packages required by testing tools")
cmdargs = parser.parse_args()

(name, version) = linux_distribution()

for distro in distros:
    if type(distro.name) == type([]):
        dname = [n for n in distro.name if name.startswith(n)]
        if len(dname):
            distro.name = dname[0]
        else:
            continue

    if name.startswith(distro.name):
        for dver in distro.versions:
            if dver.version == version or version.startswith(dver.version+'.'):
                if hasattr(distro, 'pre_install'):
                    subprocess.check_call(distro.pre_install, shell=True)
                if hasattr(dver, 'pre_install'):
                    subprocess.check_call(dver.pre_install, shell=True)
                pkg = distro.packages + dver.packages
                if cmdargs.ec2:
                    pkg += distro.ec2_packages + dver.ec2_packages
                if cmdargs.test:
                    pkg += distro.test_packages + dver.test_packages
                subprocess.check_call(distro.install + ' ' + str.join(' ', pkg), shell=True)
                if cmdargs.ec2:
                    if distro.ec2_post_install:
                        subprocess.check_call(distro.ec2_post_install, shell=True)
                    if dver.ec2_post_install:
                        subprocess.check_call(dver.ec2_post_install, shell=True)
                if hasattr(distro, 'post_install'):
                    subprocess.check_call(distro.post_install, shell=True)
                if hasattr(dver, 'post_install'):
                    subprocess.check_call(dver.post_install, shell=True)
                sys.exit(0)
        print ('Your distribution %s version %s is not supported by this script' % (name, version))
        sys.exit(1)

print('Your distribution is not supported by this script.')
sys.exit(2)
