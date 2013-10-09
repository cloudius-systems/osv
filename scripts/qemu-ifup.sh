#!/bin/sh

echo SCRIPT, $1
brctl addif $OSV_BRIDGE $1
ifconfig $1 up
