#!/bin/sh

set -x

ROOT=/zfs
IMG=`pwd`/zfs.img
LOOP_DEV=/dev/loop7
DEV=/dev/vblk1
CACHE=`pwd`/zfs.cache
POOL=osv
FS=usr

mkdir -p ${ROOT}

zpool destroy ${POOL}
rm -f ${IMG}

truncate --size 10g ${IMG}
losetup ${LOOP_DEV} ${IMG}
ln ${LOOP_DEV} ${DEV}

zpool create -f ${POOL} -o cachefile=${CACHE} -o altroot=${ROOT} ${DEV}
zfs create ${POOL}/${FS}

cp -a tests/ ${ROOT}/${FS}

zpool export ${POOL}

sleep 2

losetup -d ${LOOP_DEV}
rm ${DEV}
