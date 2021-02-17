#!/bin/bash
#
# Copyright (C) 2021 Waldemar Kozaczuk
#
# This work is open source software, licensed under the terms of the
# BSD license as described in the LICENSE file in the top-level directory.
#

# Usage:
#  ./scripts/download_and_build_boost.sh <BOOST_VERSION> <ARCH>
#
set -e

OSV_ROOT=$(dirname $(readlink -e $0))/..

BOOST_VERSION="$1"
if [[ "${BOOST_VERSION}" == "" ]]; then
  BOOST_VERSION="1.70.0"
fi
BOOST_VERSION2=${BOOST_VERSION//[.]/_}

ARCH=$2
if [[ "${ARCH}" == "" ]]; then
  ARCH=$(uname -m)
fi

TAR_DOWNLOAD_DIR="${OSV_ROOT}/build/downloaded_packages/${ARCH}/boost/upstream/"
mkdir -p "${TAR_DOWNLOAD_DIR}"

BOOST_URL=https://dl.bintray.com/boostorg/release/"${BOOST_VERSION}"/source/boost_"${BOOST_VERSION2}".tar.gz
wget -c -O "${TAR_DOWNLOAD_DIR}"/boost_"${BOOST_VERSION2}".tar.gz "${BOOST_URL}"

pushd "${TAR_DOWNLOAD_DIR}"
rm -rf "./boost_${BOOST_VERSION2}"
tar -xf "./boost_${BOOST_VERSION2}.tar.gz"
cd "./boost_${BOOST_VERSION2}"
./bootstrap.sh --with-libraries=system,thread,test,chrono,regex,date_time,filesystem,locale,random,atomic,log,program_options

BOOST_DIR=$(pwd)
if [[ "$CROSS_PREFIX" == aarch64* ]]; then
  echo "using gcc : arm : ${CROSS_PREFIX}g++ ;" > user-config.jam
fi

B2_OPTIONS="threading=multi"
if [[ "${CROSS_PREFIX}" == aarch64* ]]; then
  B2_OPTIONS="${B2_OPTIONS} --user-config=${BOOST_DIR}/user-config.jam toolset=gcc-arm architecture=arm address-model=64"
fi
./b2 ${B2_OPTIONS} -j$(nproc)

#
# Create symlinks to install boost and make it visible to OSv makefile
rm -f ../../install
ln -s upstream/boost_${BOOST_VERSION2}/stage ../../install
mkdir -p stage/usr/include
ln -s ../../../boost stage/usr/include/boost
popd
