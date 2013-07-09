#!/bin/sh

echo SCRIPT, $1
brctl addif virbr0 $1 
ifconfig $1 up
