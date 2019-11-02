#!/bin/bash

FILE="/tmp/osv_iptables.rules.old"

if [ -f "$FILE" ]; then
    sudo iptables-restore < "$FILE"
    sudo rm "$FILE"
fi
sudo sh -c "echo 0 > /proc/sys/net/ipv4/ip_forward"
