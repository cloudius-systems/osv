#!/bin/bash
#
# Copyright (C) 2021 Waldemar Kozaczuk
#
# This work is open source software, licensed under the terms of the
# BSD license as described in the LICENSE file in the top-level directory.
#

# This scripts installs all necessary packages to build QEMU from sources,
# downloads QEMU sources and builds both x86_64 and aarch64 versions of it
#
# Usage:
#  ./scripts/download_and_build_qemu.sh <QEMU_VERSION>
#

#Install tools
USER_ID=$(id -u)
LINUX_DIST_ID=$(grep "^ID=" /etc/os-release)

case "${LINUX_DIST_ID}" in
  *fedora*|*centos*)
    PACKAGES="ninja-build cmake3 glib2-devel libfdt-devel pixman-devel zlib-devel libaio-devel libcap-devel libiscsi-devel"
    if [[ "${LINUX_DIST_ID}" == "fedora" ]]; then
      PACKAGE_MANAGER=dnf
    else
      PACKAGE_MANAGER=yum
    fi ;;
  *ubuntu*)
    PACKAGES="ninja-build cmake libglib2.0-dev libfdt-dev libpixman-1-dev zlib1g-dev libaio-dev libcap-dev libnfs-dev libiscsi-dev"
    PACKAGE_MANAGER="apt-get" ;;
  *)
    echo "Unsupported distribution!" && exit 1
esac

if [[ "${USER_ID}" == "0" ]]; then
  echo "Installing all necessary packages!" && ${PACKAGE_MANAGER} install ${PACKAGES}
else
  echo "Needs super user access to install all necessary packages!" && sudo ${PACKAGE_MANAGER} install ${PACKAGES}
fi

#Download and build qemu
QEMU_VERSION=$1
QEMU_VERSION=${QEMU_VERSION:-v5.2.0}

OSV_ROOT=$(dirname $(readlink -e $0))/..
BUILD_DIR="${OSV_ROOT}"/build/downloaded_packages

mkdir -p "${BUILD_DIR}"
pushd "${BUILD_DIR}"

if [[ ! -d qemu ]]; then
  git clone --depth 1 --branch "${QEMU_VERSION}" git://git.qemu.org/qemu.git
fi
cd qemu
mkdir -p build && cd build

../configure --target-list=x86_64-softmmu,aarch64-softmmu
make -j$(nproc)
popd

echo "Built QEMU at ${BUILD_DIR}/qemu/build/qemu-system-x86_64 and ${BUILD_DIR}/qemu/build/qemu-system-aarch64"
