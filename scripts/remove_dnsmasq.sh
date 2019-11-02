#!/bin/bash

if [ "$#" -ne 1 ]; then
  echo "Usage: $0 <NIC>"
  echo "Deletes DNSmasq configuration file"
  exit 1
fi

DEV=$1
sudo rm -rf /var/lib/dnsmasq/$DEV
sudo rm -rf /etc/dnsmasq.d/$DEV.conf
