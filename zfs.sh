#!/bin/sh

set -x

IMG=`pwd`/zfs.img
LOOP_DEV=/dev/loop7
DEV=/dev/vblk1
CACHE=`pwd`/zfs.cache
POOL=osv
FS=usr


zpool destroy ${POOL}
rm -f ${IMG}

truncate --size 10g ${IMG}
losetup ${LOOP_DEV} ${IMG}
ln ${LOOP_DEV} ${DEV}

zpool create -f ${POOL} -o cachefile=${CACHE} -o altroot=/ ${DEV}
zfs create ${POOL}/${FS}

zpool export ${POOL}

sleep 2

losetup -d ${LOOP_DEV}
rm ${DEV}
