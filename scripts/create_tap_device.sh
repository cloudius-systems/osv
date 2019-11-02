#!/bin/bash

MODE=$1
TAP_INTERFACE=$2
TAP_IP=$3
BRIDGE_NAME=$3

usage()
{
  cat <<-EOF
Usage: $0 <mode> <tap_device_name> <tap_IP> | <bridge_name>
creates TAP device
mode can be: 'bridged' or 'natted'(the default)
To delete TAP device: sudo ip link set <tap_device_name> down
EOF
}

if [ "$#" -ne 3 ]; then
  usage
  exit 1
fi

ip link show "$TAP_INTERFACE" 1>/dev/null 2>/dev/null

if [ $? != 0 ]; then
  sudo ip tuntap add dev "$TAP_INTERFACE" mode tap
  sudo sysctl -q -w net.ipv4.conf.$TAP_INTERFACE.proxy_arp=1
  sudo sysctl -q -w net.ipv6.conf.$TAP_INTERFACE.disable_ipv6=1
  sudo ip link set dev "$TAP_INTERFACE" up

  if [ "$MODE" == "bridged" ] && [ "$BRIDGE_NAME" != "" ]; then
    sudo brctl addif "$BRIDGE_NAME" "$TAP_INTERFACE"
  else
    sudo ip addr add "$TAP_IP/30" dev "$TAP_INTERFACE"
  fi
else
  echo "The tap device $TAP_INTERFACE already exists"
  exit 0
fi
