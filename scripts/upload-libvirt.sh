#!/bin/sh -e
#
#  Copyright (C) 2015 Cloudius Systems, Ltd.
#
#  This work is open source software, licensed under the terms of the
#  BSD license as described in the LICENSE file in the top-level directory.
#

NAME=$1
MODE=${2:-release}
DIR=${3:-/var/lib/libvirt/images}

if [ "$NAME" == "" ]; then
    echo "usage: $0 vm_name [mode] [image dir]"
    exit
fi

cp build/$MODE/usr.img "$DIR/$NAME.qcow2"

# Since virt-install boots VM and waits guest OS shutdown,
# so we need some program which exits immediately after bootup,
# and zfs.so was chosen for it.
./scripts/imgedit.py setargs "$DIR/$NAME.qcow2" "/zfs.so"

virt-install --import --name="$NAME" --ram=4096 --vcpus=4 --disk path="$DIR/$NAME.qcow2",bus=virtio --os-variant=none --accelerate --network=network:default,model=virtio --serial pty --cpu host --rng=/dev/random

# Restore default cmdline
./scripts/imgedit.py setargs "$DIR/$NAME.qcow2" "`cat build/$MODE/cmdline`"
