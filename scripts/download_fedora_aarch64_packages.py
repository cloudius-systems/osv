#!/usr/bin/python3

import subprocess, os, string, sys
from linux_distro import linux_distribution

def aarch64_download(version):
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
    osv_root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
    script_path = '%s/scripts/download_rpm_package.sh' % osv_root
    destination = '%s/build/downloaded_packages/aarch64' % osv_root

    install_commands = ['%s %s %s %s/gcc' % (script_path, package, version, destination) for package in gcc_packages]
    install_commands += ['%s %s %s %s/boost' % (script_path, package, version, destination) for package in boost_packages]
    install_commands = ['rm -rf %s/gcc/install' % destination,
                        'rm -rf %s/boost/install' % destination] + install_commands
    return ' && '.join(install_commands)

(name, version) = linux_distribution()
if name.lower() != 'fedora':
    print("The distribution %s is not supported for cross-compiling aarch64 version of OSv" % name)
    sys.exit(1)

print('Downloading aarch64 packages to cross-compile ARM version ...')
subprocess.check_call(aarch64_download(version), shell=True)
print('Downloaded all aarch64 packages!')
