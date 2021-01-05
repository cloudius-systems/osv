#!/usr/bin/python3

import subprocess, os, string, sys
from distro import linux_distribution
import re

osv_root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
destination = '%s/build/downloaded_packages/aarch64' % osv_root

def fedora_download_commands(fedora_version):
    gcc_packages = ['gcc',
                    'glibc',
                    'glibc-devel',
                    'libgcc',
                    'libstdc++',
                    'libstdc++-devel',
                    'libstdc++-static']
    boost_packages = ['boost-devel',
                      'boost-static',
                      'boost-system',
                      'boost-filesystem',
                      'boost-test',
                      'boost-chrono',
                      'boost-timer']
    script_path = '%s/scripts/download_fedora_aarch64_rpm_package.sh' % osv_root

    install_commands = ['%s %s %s %s/gcc' % (script_path, package, fedora_version, destination) for package in gcc_packages]
    install_commands += ['%s %s %s %s/boost' % (script_path, package, fedora_version, destination) for package in boost_packages]
    install_commands = ['rm -rf %s/gcc/install' % destination,
                        'rm -rf %s/boost/install' % destination] + install_commands
    return ' && '.join(install_commands)

def ubuntu_download_commands(boost_version):
    boost_packages = ['libboost%s-dev' % boost_version,
                      'libboost-system%s' % boost_version,
                      'libboost-system%s-dev' % boost_version,
                      'libboost-filesystem%s' % boost_version,
                      'libboost-filesystem%s-dev' % boost_version,
                      'libboost-test%s' % boost_version,
                      'libboost-test%s-dev' % boost_version,
                      'libboost-timer%s' % boost_version,
                      'libboost-timer%s-dev' % boost_version,
                      'libboost-chrono%s' % boost_version,
                      'libboost-chrono%s-dev' % boost_version]

    script_path = '%s/scripts/download_ubuntu_aarch64_deb_package.sh' % osv_root

    install_commands = ['%s boost%s %s %s/boost' % (script_path, boost_version, package, destination) for package in boost_packages]
    install_commands = ['rm -rf %s/boost/install' % destination] + install_commands
    return ' && '.join(install_commands)

def ubuntu_identify_boost_version(codename, index):
    packages = subprocess.check_output(['wget', '-t', '1', '-qO-', 'http://ports.ubuntu.com/indices/override.%s.%s' % (codename, 'main')]).decode('utf-8')
    libboost_system_package = re.search("libboost-system\d+\.\d+-dev", packages)
    if libboost_system_package:
       libboost_system_package_name = libboost_system_package.group()
       return re.search('\d+\.\d+', libboost_system_package_name).group()
    else:
       return ''

(name, version, codename) = linux_distribution()
if name.lower() == 'fedora':
    commands_to_download = fedora_download_commands(version)
elif name.lower() == 'ubuntu':
    boost_version = ubuntu_identify_boost_version(codename, 'main')
    if boost_version == '':
        boost_version = ubuntu_identify_boost_version(codename, 'universe')
    if boost_version == '':
        print("Cound not find boost version from neither main nor universe ports index!")
        sys.exit(1)
    commands_to_download = ubuntu_download_commands(boost_version)
else:
    print("The distribution %s is not supported for cross-compiling aarch64 version of OSv" % name)
    sys.exit(1)

print('Downloading aarch64 packages to cross-compile ARM version ...')
subprocess.check_call(commands_to_download, shell=True)
print('Downloaded all aarch64 packages!')
