#!/bin/bash

usage()
{
  cat <<-EOF
usage: $0 <mode> <tap_device_name> (<tap_IP> <physical_NIC>) | <bridge_name>
sets up networking for Firecracker
mode can be: 'bridged', 'natted' or 'clean' (restores IP tables and deletes dnsmasq)
EOF
}

if [ "$#" -lt 2 ]; then
  usage
  exit 1
fi

MODE=$1
TAP_INTERFACE=$2
TAP_IP=$3
PHYSICAL_NIC=$4

BRIDGE_NAME=$3

THIS_DIR=$(dirname $0)

#Create TAP device
if [ "$MODE" == "bridged" ] && [ "$BRIDGE_NAME" != "" ]; then
  $THIS_DIR/create_tap_device.sh "bridged" "$TAP_INTERFACE" "$BRIDGE_NAME"
elif [ "$MODE" == "clean" ]; then
  $THIS_DIR/remove_dnsmasq.sh "$TAP_INTERFACE"
  $THIS_DIR/restore_iptables.sh
elif [ "$MODE" == "natted" ]; then
  if [ "$#" -lt 3 ]; then
    usage
    exit 1
  fi
  $THIS_DIR/create_tap_device.sh "natted" "$TAP_INTERFACE" "$TAP_IP"

  if [ "$PHYSICAL_NIC" != "" ]; then
    #Forwards traffic out of TAP device to the physical NIC
    $THIS_DIR/setup_iptables.sh "$PHYSICAL_NIC" "$TAP_INTERFACE"

    #Setup local DNS server
    $THIS_DIR/setup_dnsmasq.sh "$TAP_INTERFACE"
    echo "Set up IP forwarding $TAP_INTERFACE -> $PHYSICAL_NIC and light DNS server for $TAP_INTERFACE"
  else
    echo "To make outgoing traffic work pass physical NIC name"
  fi
else
  usage
  exit 1
fi
