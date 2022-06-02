#!/usr/bin/python3

import subprocess, os, string, sys
import distro
import re

osv_root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
destination = '%s/build/downloaded_packages/aarch64' % osv_root

def fedora_download_commands(fedora_version):
    #For some reason, the gcc-c++-aarch64-linux-gnu package Fedora 34 ships with,
    #installs the version 10.2.1 of gcc aarch64 cross-compiler which is different
    #from the regular x86_64 gcc compiler - 11.1.1. To avoid compilation errors caused
    #by the mismatch between compiler and aarch64 C/C++ headers Fedora 34 provides,
    #we download all relevant artifacts from Fedora 33 which also provides
    #the version 10.2.1 of gcc aarch64 cross-compiler and therefore compatible C/C++
    #headers.
    if fedora_version == '34':
        gcc_version = subprocess.check_output(["aarch64-linux-gnu-gcc", "--version"]).decode('utf-8')
        if gcc_version.find('(GCC) 10.2.1') >= 0:
            fedora_version = '33'

    gcc_packages = ['gcc',
                    'glibc',
                    'glibc-devel',
                    'kernel-headers',
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

def ubuntu_download_commands(boost_long_version):
    boost_short_version = re.search('\d+\.\d+', boost_long_version).group()
    boost_patchlevel = boost_long_version.split('.')[2]
    boost_packages = ['libboost%s-dev' % boost_short_version,
                      'libboost-system%s' % boost_short_version,
                      'libboost-system%s-dev' % boost_short_version,
                      'libboost-filesystem%s' % boost_short_version,
                      'libboost-filesystem%s-dev' % boost_short_version,
                      'libboost-test%s' % boost_short_version,
                      'libboost-test%s-dev' % boost_short_version,
                      'libboost-timer%s' % boost_short_version,
                      'libboost-timer%s-dev' % boost_short_version,
                      'libboost-chrono%s' % boost_short_version,
                      'libboost-chrono%s-dev' % boost_short_version]

    script_path = '%s/scripts/download_ubuntu_aarch64_deb_package.sh' % osv_root

    if boost_patchlevel == '0':
        boost_package_directory = boost_short_version
    else:
        boost_package_directory = boost_long_version

    install_commands = ['%s boost%s %s %s/boost' % (script_path, boost_package_directory, package, destination) for package in boost_packages]
    install_commands = ['rm -rf %s/boost/install' % destination] + install_commands
    return ' && '.join(install_commands)

def ubuntu_identify_boost_version(codename, index):
    packages = subprocess.check_output(['wget', '-t', '1', '-qO-', 'http://ports.ubuntu.com/indices/override.%s.%s' % (codename, 'main')]).decode('utf-8')
    libboost_system_package = re.search("libboost-system\d+\.\d+\.\d+", packages)
    if libboost_system_package:
       libboost_system_package_name = libboost_system_package.group()
       return re.search('\d+\.\d+\.\d+', libboost_system_package_name).group()
    else:
       return ''

name = distro.id()
version = distro.version()
codename = distro.lsb_release_attr('codename')
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
elif name.lower() == "centos":
    commands_to_download = [ 'bash -eu %s/scripts/download_aarch64_toolchain.sh' % osv_root ]
else:
    print("The distribution %s is not supported for cross-compiling aarch64 version of OSv" % name)
    sys.exit(1)

print('Downloading aarch64 packages to cross-compile ARM version ...')
subprocess.check_call(commands_to_download, shell=True)
print('Downloaded all aarch64 packages!')
