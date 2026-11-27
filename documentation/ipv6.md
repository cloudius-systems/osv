The IPv6 support is optional and can be enabled/disabled using the
conf-INET6 option in the conf/base.mk file.

Currently there are 2 known remaining issues left which are due to
limitations of the FreeBSD 9.0 IPv6 code which this code is base off of.

1. The NDP code is not thread safe
   The FreeBSD 9.0 IPv6 code is not MPSAFE and expects callouts to run
   with the giant lock and uses splnet() to ensure thread safety.
   Unfortunatley OSv does not support these mechanisms and it is pretty
   tricky to modify the FreeBSD 9.0 IPv6 code to make it thread safe
   without them due to multiple locks required in the NDP code.

2. IPv6 MAC cache entries can expire causing extra neighbor solicits
   This does not occur when using TCP sockets which keep the cache
   entry active using ND6_HINT() but can occur for UDP or raw IP.

These issues appear to be fixed in FreeBSD 11, however back porting these
fixes is pretty involved and impacts a lot of the existing FreeBSD
code not only IPv6.

IPv6 Stateless Autoconfiguration is enabled, but has not been tested yet.

DHCPv6 is not supported yet.

Static IPv6 addresses may be configured using command line arguments:

./scripts/run.py  --execute \
    "--ip=eth0,2001:1:1::501,64 --defaultgw=2001:1:1::1 \
     /tools/iperf -s -V -B ::"

or using cloud-init network version 1 yaml which is also added with
this patch series:

#cloud-config
network:
    version: 1
    config:
    - type: physical
      name: eth0
      subnets:
          - type: static
            address: 2001:1:1::501/64
