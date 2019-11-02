#!/bin/bash
# Configures local light DNS service for specified NIC
if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <NIC>"
    exit 1
fi
DEV=$1

if [ ! -d "/var/lib/dnsmasq/$DEV" ]; then
  sudo mkdir -p /var/lib/dnsmasq/$DEV
  sudo touch /var/lib/dnsmasq/$DEV/hostsfile
  sudo touch /var/lib/dnsmasq/$DEV/leases
  sudo touch /var/lib/dnsmasq/$DEV/dnsmasq.conf
  sudo sh -c "cat << 'EOF' >/var/lib/dnsmasq/$DEV/dnsmasq.conf
except-interface=lo
interface=$DEV
bind-dynamic
strict-order
EOF"
else
  echo "Dnsmasq configured for $DEV"
fi

if [ ! -f "/etc/dnsmasq.d/$DEV.conf" ]; then
  sudo mkdir -p /etc/dnsmasq.d/
  sudo touch /etc/dnsmasq.d/$DEV.conf
  sudo bash -c "echo "except-interface=$DEV" >> /etc/dnsmasq.d/$DEV.conf"
  sudo bash -c "echo "bind-interfaces" >> /etc/dnsmasq.d/$DEV.conf"
else
  echo "Dnsmasq configured for $DEV"
fi

DNS_MASK_RUNNING=$(ps -ef | grep dnsmasq | grep -v "$0" | grep "$DEV")
if [ "$DNS_MASK_RUNNING" == "" ]; then
  sudo mkdir -p /var/run/dnsmasq/
  sudo dnsmasq --conf-file=/var/lib/dnsmasq/$DEV/dnsmasq.conf --pid-file=/var/run/dnsmasq/$DEV.pid
else
  echo "Dnsmasq running for $DEV"
fi
