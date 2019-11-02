#!/bin/bash

if [ "$#" -ne 2 ]; then
  echo "Usage: $0 <physical_NIC> <tap_NIC>"
  echo "sets up forwarding from TAP device to physical NIC - wired or wireless"
  exit 1
fi

#Sets up forwarding for specified tap device and physical interface
PHYSICAL_INTERFACE=$1
TAP_DEVICE=$2

sudo iptables-save > /tmp/osv_iptables.rules.old
sudo sh -c "echo 1 > /proc/sys/net/ipv4/ip_forward"
sudo iptables -t nat -A POSTROUTING -o $PHYSICAL_INTERFACE -j MASQUERADE
sudo iptables -A FORWARD -m conntrack --ctstate RELATED,ESTABLISHED -j ACCEPT
sudo iptables -A FORWARD -i $TAP_DEVICE -o $PHYSICAL_INTERFACE -j ACCEPT
sudo iptables -A INPUT -i $TAP_DEVICE -p udp -m udp -m multiport --dports 53 -j ACCEPT
sudo iptables -A INPUT -i $TAP_DEVICE -p tcp -m tcp -m multiport --dports 53 -j ACCEPT
