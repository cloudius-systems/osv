#!/bin/bash
OSV_BASE=$1

BOOST_BASE=$OSV_BASE/build/downloaded_packages/aarch64/boost/install
BOOST_LIB_DIR=$(readlink -f $(dirname $(find $BOOST_BASE/ -name libboost_system.so)))

echo "/usr/lib/$(basename $(readlink -f $BOOST_LIB_DIR/libboost_system.so)): $BOOST_LIB_DIR/libboost_system.so"
echo "/usr/lib/$(basename $(readlink -f $BOOST_LIB_DIR/libboost_timer.so)): $BOOST_LIB_DIR/libboost_timer.so"
echo "/usr/lib/$(basename $(readlink -f $BOOST_LIB_DIR/libboost_unit_test_framework.so)): $BOOST_LIB_DIR/libboost_unit_test_framework.so"
echo "/usr/lib/$(basename $(readlink -f $BOOST_LIB_DIR/libboost_chrono.so)): $BOOST_LIB_DIR/libboost_chrono.so"
echo "/usr/lib/$(basename $(readlink -f $BOOST_LIB_DIR/libboost_filesystem.so)): $BOOST_LIB_DIR/libboost_filesystem.so"
