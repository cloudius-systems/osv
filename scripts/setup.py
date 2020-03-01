#!/usr/bin/python3

# set up a development environment for OSv.  Run as root.

import sys, argparse
import subprocess, os

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
    install = 'yum -y install --allowerasing --forcearch x86_64'
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
                'make',
                'maven',
                'maven-shade-plugin',
                'ncurses',
                'ncurses-devel',
                'openssl',
                'openssl-libs',
                'p11-kit',
                'patch',
                'python3-dpkt',
                'qemu-img',
                'qemu-system-x86',
                'tcpdump',
                'unzip',
                'wget',
                'yaml-cpp-devel',
                'pax-utils',
                 ]
    ec2_packages = standard_ec2_packages
    test_packages = ['openssl-devel']
    ec2_post_install = standard_ec2_post_install

    def aarch64_download(self, version):
        gcc_packages = ['gcc',
                        'glibc',
                        'glibc-devel',
                        'libgcc',
                        'libstdc++',
                        'libstdc++-devel',
                        'libstdc++-static']
        boost_packages = ['boost-devel',
                          'boost-static',
                          'boost-system']
        osv_root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
        script_path = '%s/scripts/download_rpm_package.sh' % osv_root
        destination = '%s/downloaded_packages/aarch64' % osv_root
        ##
        # The setup.py is typically run as root to allow yum properly install packages
        # This however would cause all files downloaded to downloaded_packages/aarch64 directory
        # get created and owned by the root user which in most cases is not desirable
        # To prevent that let us compare current process user id with the owner id of osv root
        # directory and if different run all download command with the same user as the one owning
        # the root directory
        current_user_id = os.getuid()
        osv_root_owner_id = os.stat(osv_root).st_uid
        if current_user_id != osv_root_owner_id and current_user_id == 0:
            command_prefix = "sudo -u '#%d'" % osv_root_owner_id # Most likely setup.py is run by root so let us use sudo
        else:
            command_prefix = ''

        install_commands = ['%s %s %s %s %s/gcc' % (command_prefix, script_path, package, version, destination) for package in gcc_packages]
        install_commands += ['%s %s %s %s %s/boost' % (command_prefix, script_path, package, version, destination) for package in boost_packages]
        install_commands = ['%s rm -rf %s/gcc/install' % (command_prefix, destination),
                            '%s rm -rf %s/boost/install' % (command_prefix, destination)] + install_commands
        return ' && '.join(install_commands)

    class Fedora_25(object):
        packages = ['java-1.8.0-openjdk', 'python2-requests', 'openssl-devel', 'lua-5.3.*', 'lua-devel-5.3.*']
        ec2_packages = []
        test_packages = []
        ec2_post_install = None
        version = '25'

    class Fedora_26(object):
        packages = ['java-1.8.0-openjdk', 'python2-requests', 'openssl-devel', 'lua-5.3.*', 'lua-devel-5.3.*']
        ec2_packages = []
        test_packages = []
        ec2_post_install = None
        version = '26'

    class Fedora_27(object):
        packages = ['java-1.8.0-openjdk', 'python2-requests', 'openssl-devel', 'lua-5.3.*', 'lua-devel-5.3.*']
        ec2_packages = []
        test_packages = []
        ec2_post_install = None
        version = '27'

    class Fedora_28(object):
        packages = ['java-1.8.0-openjdk', 'python2-requests', 'openssl-devel', 'lua-5.3.*', 'lua-devel-5.3.*']
        ec2_packages = []
        test_packages = []
        ec2_post_install = None
        version = '28'

    class Fedora_29(object):
        packages = ['java-1.8.0-openjdk', 'python2-requests', 'openssl-devel', 'lua-5.3.*', 'lua-devel-5.3.*']
        ec2_packages = []
        test_packages = []
        ec2_post_install = None
        version = '29'

    class Fedora_30(object):
        packages = ['java-1.8.0-openjdk', 'python2-requests', 'openssl-devel', 'lua-5.3.*', 'lua-devel-5.3.*']
        ec2_packages = []
        test_packages = []
        ec2_post_install = None
        version = '30'

    class Fedora_31(object):
        packages = ['java-1.8.0-openjdk', 'python2-requests', 'openssl-devel', 'lua-5.3.*', 'lua-devel-5.3.*']
        ec2_packages = []
        test_packages = []
        ec2_post_install = None
        version = '31'

    versions = [Fedora_25, Fedora_26, Fedora_27, Fedora_28, Fedora_29, Fedora_30, Fedora_31]

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
                'automake',
                'bison',
                'build-essential',
                'curl',
                'flex',
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
                'libyaml-cpp-dev',
                'make',
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
                'libyaml-cpp-dev',
                'make',
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
                'lua5.3',
                'liblua5.3',
                'pax-utils',
                ]

    ec2_packages = standard_ec2_packages
    test_packages = ['libssl-dev', 'zip']
    ec2_post_install = None

    class Ubuntu_19_04(object):
        packages = ['openjdk-8-jdk', 'bridge-utils', 'libvirt-daemon-system', 'libvirt-clients']
        ec2_packages = ['ec2-api-tools', 'awscli']
        test_packages = []
        ec2_post_install = None
        version = '19.04'

    class Ubuntu_18_10(object):
        packages = ['openjdk-8-jdk', 'bridge-utils', 'libvirt-daemon-system', 'libvirt-clients']
        ec2_packages = ['ec2-api-tools', 'awscli']
        test_packages = []
        ec2_post_install = None
        version = '18.10'

    class Ubuntu_18_04(object):
        packages = ['openjdk-8-jdk', 'bridge-utils', 'libvirt-bin']
        ec2_packages = ['ec2-api-tools', 'awscli']
        test_packages = []
        ec2_post_install = None
        version = '18.04'

    class Ubuntu_17_04(object):
        packages = ['openjdk-8-jdk', 'libvirt-bin']
        ec2_packages = ['ec2-api-tools', 'awscli']
        test_packages = []
        ec2_post_install = None
        version = '17.04'

    class Ubuntu_16_04(object):
        packages = ['openjdk-8-jdk', 'libvirt-bin']
        ec2_packages = ['ec2-api-tools', 'awscli']
        test_packages = []
        ec2_post_install = None
        version = '16.04'

    versions = [Ubuntu_19_04, Ubuntu_18_10, Ubuntu_18_04, Ubuntu_17_04, Ubuntu_16_04]

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

def linux_distribution():
    def parse_file(f):
        res = {}
        for line in f:
            k, v = line.rstrip().split('=')
            res[k] = v.strip('"')
        return res

    try:
        with open('/etc/os-release') as f:
            info = parse_file(f)
            return (info['NAME'], info['VERSION_ID'])
    except FileNotFoundError:
        try:
            with open('/etc/lsb-release') as f:
                info = parse_file(f)
                return (info['DISTRIB_ID'], info['DISTRIB_RELEASE'])
        except FileNotFoundError:
            print('Could not find linux distribution file!')
            return ('Unknown', 'Unknown')

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
                pkg = distro.packages + dver.packages
                if cmdargs.ec2:
                    pkg += distro.ec2_packages + dver.ec2_packages
                if cmdargs.test:
                    pkg += distro.test_packages + dver.test_packages
                subprocess.check_call(distro.install + ' ' + str.join(' ', pkg), shell=True)
                if 'aarch64_download' in dir(distro):
                    print('Downloading aarch64 packages to cross-compile ARM version ...')
                    subprocess.check_call(distro.aarch64_download(dver.version), shell=True)
                    print('Downloaded all aarch64 packages!')
                if cmdargs.ec2:
                    if distro.ec2_post_install:
                        subprocess.check_call(distro.ec2_post_install, shell=True)
                    if dver.ec2_post_install:
                        subprocess.check_call(dver.ec2_post_install, shell=True)
                sys.exit(0)
        print ('Your distribution %s version %s is not supported by this script' % (name, version))
        sys.exit(1)

print('Your distribution is not supported by this script.')
sys.exit(2)
