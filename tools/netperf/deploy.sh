#!/bin/sh -ex

rm -rf netperf-2.6.0.tar.bz netperf-2.6.0

wget ftp://ftp.netperf.org/netperf/netperf-2.6.0.tar.bz2
tar jxf netperf-2.6.0.tar.bz2
cd netperf-2.6.0
./configure
make
sudo make install
patch -p1 < ../0001-netperf-socket-timeout-for-send-receive-operations.patch
make clean
make CFLAGS="-fPIC -shared"
mkdir osv
cp src/netserver osv/netserver.so
