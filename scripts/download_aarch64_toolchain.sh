#!/bin/bash -eu

# Copyright (c) 2021, DornerWorks, Ltd.
# Author: Stewart Hildebrand
# SPDX-License-Identifier: BSD-3-Clause OR MIT

# Usage:
# $ source download_aarch64_toolchain.sh
# or
# $ . download_aarch64_toolchain.sh

# https://developer.arm.com/tools-and-software/open-source-software/developer-tools/gnu-toolchain/gnu-a/downloads

ARM64_TOOLCHAIN_SCRIPTDIR="$(cd $(dirname ${BASH_SOURCE}) && pwd)/"
ARM64_TOOLCHAIN_DESTINATION="${ARM64_TOOLCHAIN_SCRIPTDIR}../build/downloaded_packages/aarch64/toolchain/"
ARM64_TOOLCHAIN_VERSION=9.2-2019.12
ARM64_TOOLCHAIN_FILENAME=gcc-arm-9.2-2019.12-x86_64-aarch64-none-linux-gnu

if [ ! -d ${ARM64_TOOLCHAIN_DESTINATION}${ARM64_TOOLCHAIN_FILENAME} ]; then
    mkdir -p "${ARM64_TOOLCHAIN_DESTINATION}"
    pushd "${ARM64_TOOLCHAIN_DESTINATION}" >/dev/null
    if [ ! -s ${ARM64_TOOLCHAIN_FILENAME}.tar.xz ]; then
        wget https://developer.arm.com/-/media/Files/downloads/gnu-a/${ARM64_TOOLCHAIN_VERSION}/binrel/${ARM64_TOOLCHAIN_FILENAME}.tar.xz
    fi
    tar -xf ${ARM64_TOOLCHAIN_FILENAME}.tar.xz
    popd >/dev/null
fi

if [ -d /usr/lib64/ccache ]; then
    CCACHE_SYMLINK_DIR=/usr/lib64/ccache
fi
if [ -d /usr/lib/ccache ]; then
    CCACHE_SYMLINK_DIR=/usr/lib/ccache
fi

PATH=${ARM64_TOOLCHAIN_DESTINATION}${ARM64_TOOLCHAIN_FILENAME}/bin:${PATH}
if [ ! -z "${CCACHE_SYMLINK_DIR-}" ]; then
    PATH=${CCACHE_SYMLINK_DIR}:$(echo ${PATH} | sed "s|${CCACHE_SYMLINK_DIR}:||g")
fi

if [ -z "${CCACHE_SYMLINK_DIR-}" ]; then
    echo "It is recommended to install ccache and create symlinks for the"
    echo "aarch64 toolchain in order to significantly speed up the build."
else
    if [ ! -h "${CCACHE_SYMLINK_DIR}/aarch64-none-linux-gnu-g++" ] || [ ! -h "${CCACHE_SYMLINK_DIR}/aarch64-none-linux-gnu-gcc" ]; then
        echo "It is recommended to create ccache symlinks for the aarch64"
        echo "toolchain in order to significantly speed up the build. Run the"
        echo "following commands to create the appropriate symlinks:"
        echo ""
        echo "  sudo ln -s ../../bin/ccache \"${CCACHE_SYMLINK_DIR}/aarch64-none-linux-gnu-gcc\""
        echo "  sudo ln -s ../../bin/ccache \"${CCACHE_SYMLINK_DIR}/aarch64-none-linux-gnu-g++\""
        echo ""
        echo "After this, please re-source the toolchain script to enable ccache."
    fi
fi
