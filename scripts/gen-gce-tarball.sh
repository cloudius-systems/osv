#!/bin/sh

origin_img=usr.img
gce_img=disk.raw
gce_tarball=osv.tar.gz
dir=build/release
cd $dir
rm -f $gce_img $gce_tarball
qemu-img convert $origin_img -O raw $gce_img
tar -Szcf $gce_tarball $gce_img
cd ../..

echo "$PWD/$dir/$gce_tarball is created"
