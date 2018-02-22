#!/usr/bin/python2

# set up a development environment for OSv.  Run as root.

import sys, platform, argparse
import subprocess

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
    install = 'yum -y install --allowerasing'
    packages = [
                'ant',
                'autoconf',
                'automake',
                'bison',
                'boost-static',
                'curl',
                'flex',
                'gcc-c++',
                'gcc-c++-aarch64-linux-gnu',
                'gdb',
                'genromfs',
                'git',
                'gnutls-utils',
                'libedit-devel',
                'libstdc++-static',
                'libtool',
                'libvirt',
                'maven',
                'maven-shade-plugin',
                'ncurses',
                'ncurses-devel',
                'openssl',
                'openssl-libs',
                'p11-kit',
                'patch',
                'python-dpkt',
                'qemu-img',
                'qemu-system-x86',
                'tcpdump',
                'unzip',
                'wget',
                'yaml-cpp-devel',
                 ]
    ec2_packages = standard_ec2_packages
    test_packages = ['openssl-devel']
    ec2_post_install = standard_ec2_post_install

    class Fedora_19(object):
        packages = ['java-1.7.0-openjdk', 'python-requests', 'openssl-devel']
        ec2_packages = []
        test_packages = []
        ec2_post_install = None
        version = '19'

    class Fedora_20(object):
        packages = ['java-1.7.0-openjdk', 'python-requests', 'openssl-devel']
        ec2_packages = []
        test_packages = []
        ec2_post_install = None
        version = '20'

    class Fedora_21(object):
        packages = ['java-1.7.0-openjdk', 'python-requests', 'openssl-devel']
        ec2_packages = []
        test_packages = []
        ec2_post_install = None
        version = '21'

    class Fedora_22(object):
        packages = ['java-1.8.0-openjdk', 'python-requests', 'openssl-devel']
        ec2_packages = []
        test_packages = []
        ec2_post_install = None
        version = '22'

    class Fedora_23(object):
        packages = ['java-1.8.0-openjdk', 'python2-requests', 'openssl-devel']
        ec2_packages = []
        test_packages = []
        ec2_post_install = None
        version = '23'

    class Fedora_24(object):
        packages = ['java-1.8.0-openjdk', 'python2-requests', 'openssl-devel']
        ec2_packages = []
        test_packages = []
        ec2_post_install = None
        version = '24'

    class Fedora_25(object):
        packages = ['java-1.8.0-openjdk', 'python2-requests', 'openssl-devel']
        ec2_packages = []
        test_packages = []
        ec2_post_install = None
        version = '25'

    class Fedora_26(object):
        packages = ['java-1.8.0-openjdk', 'python2-requests', 'compat-openssl10-devel']
        ec2_packages = []
        test_packages = []
        ec2_post_install = None
        version = '26'

    class Fedora_27(object):
        packages = ['java-1.8.0-openjdk', 'python2-requests', 'compat-openssl10-devel']
        ec2_packages = []
        test_packages = []
        ec2_post_install = None
        version = '27'

    versions = [Fedora_19, Fedora_20, Fedora_21, Fedora_22, Fedora_23, Fedora_24, Fedora_25, Fedora_26, Fedora_27]

class RHELbased(Fedora):
    name = ['Scientific Linux', 'NauLinux', 'CentOS Linux', 'Red Hat Enterprise Linux', 'Oracle Linux']

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
                'build-essential',
                'gawk',
                'gdb',
                'genromfs',
                'gnutls-bin',
                'lib32stdc++-4.9-dev',
                'libboost-all-dev',
                'libedit-dev',
                'libmaven-shade-plugin-java',
                'libncurses5-dev',
                'libssl-dev',
                'libtool',
                'maven',
                'openjdk-7-jdk',
                'openssl',
                'p11-kit',
                'python-dpkt',
                'python-requests',
                'qemu-system-x86',
                'qemu-utils',
                'tcpdump',
                ]

    ec2_packages = standard_ec2_packages
    test_packages = ['libssl-dev', 'zip']
    ec2_post_install = None

    class debian(object):
        packages = []
        ec2_packages = []
        test_packages = []
        ec2_post_install = None
        version = 'jessie/sid'

    versions = [debian]

class Ubuntu(object):
    name = 'Ubuntu'
    install = 'apt-get -y install'
    packages = [
                'ant',
                'autoconf',
                'automake',
                'bison',
                'build-essential',
                'curl',
                'flex',
                'g++-multilib',
                'gawk',
                'gdb',
                'genromfs',
                'git',
                'gnutls-bin',
                'libboost-all-dev',
                'libedit-dev',
                'libmaven-shade-plugin-java',
                'libncurses5-dev',
                'libssl-dev',
                'libtool',
                'libvirt-bin',
                'libyaml-cpp-dev',
                'maven',
                'openssl',
                'p11-kit',
                'python-dpkt',
                'python-requests',
                'qemu-system-x86',
                'qemu-utils',
                'tcpdump',
                'unzip',
                'wget',
                ]

    ec2_packages = standard_ec2_packages
    test_packages = ['libssl-dev', 'zip']
    ec2_post_install = None

    class Ubuntu_17_04(object):
        packages = ['openjdk-8-jdk']
        ec2_packages = ['ec2-api-tools', 'awscli']
        test_packages = []
        ec2_post_install = None
        version = '17.04'

    class Ubuntu_16_04(object):
        packages = ['openjdk-8-jdk']
        ec2_packages = ['ec2-api-tools', 'awscli']
        test_packages = []
        ec2_post_install = None
        version = '16.04'

    class Ubuntu_15_04(object):
        packages = ['openjdk-7-jdk']
        ec2_packages = ['ec2-api-tools', 'awscli']
        test_packages = []
        ec2_post_install = None
        version = '15.04'

    class Ubuntu_14_04(object):
        packages = ['openjdk-7-jdk']
        ec2_packages = ['ec2-api-tools', 'awscli']
        test_packages = []
        ec2_post_install = None
        version = '14.04'

    class Ubuntu_13_10(object):
        packages = ['openjdk-7-jdk']
        ec2_packages = []
        test_packages = []
        ec2_post_install = standard_ec2_post_install
        version = '13.10'

    versions = [Ubuntu_17_04, Ubuntu_16_04, Ubuntu_15_04, Ubuntu_14_04, Ubuntu_13_10]

class LinuxMint(Ubuntu):
    name = 'LinuxMint'

    class LinuxMint_18_03(object):
        packages = ['openjdk-8-jdk']
        ec2_packages = ['ec2-api-tools', 'awscli']
        test_packages = []
        ec2_post_install = None
        version = '18.3'

    versions = [LinuxMint_18_03]

distros = [
           Debian(),
           Fedora(),
           Ubuntu(),
           LinuxMint(),
           RHELbased()
           ]

parser = argparse.ArgumentParser(prog='setup')
parser.add_argument("-e", "--ec2", action="store_true",
                    help="install packages required by EC2 release and test scripts")
parser.add_argument("-t", "--test", action="store_true",
                    help="install packages required by testing tools")
cmdargs = parser.parse_args()

(name, version, id) = platform.linux_distribution()

for distro in distros:
    if type(distro.name) == type([]):
        dname = filter(lambda n: name.startswith(n), distro.name)
        if len(dname):
            distro.name = dname[0]
        else:
            continue

    if name.startswith(distro.name):
        for dver in distro.versions:
            if dver.version == version or version.startswith(dver.version+'.'):
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
                sys.exit(0)
        print ('Your distribution %s version %s is not supported by this script' % (name, version))
        sys.exit(1)

print 'Your distribution is not supported by this script.'
sys.exit(2)
