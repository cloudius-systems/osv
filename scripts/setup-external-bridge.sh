#!/bin/bash

# This script setup a bridge that binds physical NIC
# to it and allows exposing OSv guest IP on external network

if [[ $(whoami) != "root" ]]; then
  echo "This script needs to be run as root!"
  exit -1
fi

NIC_NAME=$1
if [[ "$NIC_NAME" == "" ]]; then
  echo "Usage: ./scripts/setup-external-bridge.sh <NIC_NAME> <BRIDGE_NAME> (optional)"
  exit -1
fi

BRIDGE_NAME=$2
if [[ "$BRIDGE_NAME" == "" ]]; then
  BRIDGE_NAME="virbr1"
fi

ip addr flush dev $NIC_NAME          # Clear IP address on physical ethernet device
brctl addbr $BRIDGE_NAME             # Create bridge
brctl addif $BRIDGE_NAME $NIC_NAME   # Add physical ethernet device to bridge
sysctl -q -w net.ipv6.conf.$BRIDGE_NAME.disable_ipv6=1
dhclient -v $BRIDGE_NAME             # Grab IP addres from DHCP server
echo "Setup bridge: $BRIDGE_NAME"
