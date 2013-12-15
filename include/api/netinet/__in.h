/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */


#ifndef OSV__IN_H_
#define OSV__IN_H_

#define IPPROTO_IP       0
#define IPPROTO_HOPOPTS  0
#define IPPROTO_ICMP     1
#define IPPROTO_IGMP     2
#define IPPROTO_IPIP     4
#define IPPROTO_TCP      6
#define IPPROTO_EGP      8
#define IPPROTO_PUP      12
#define IPPROTO_UDP      17
#define IPPROTO_IDP      22
#define IPPROTO_TP       29
#define IPPROTO_DCCP     33
#define IPPROTO_IPV6     41
#define IPPROTO_ROUTING  43
#define IPPROTO_FRAGMENT 44
#define IPPROTO_RSVP     46
#define IPPROTO_GRE      47
#define IPPROTO_ESP      50
#define IPPROTO_AH       51
#define IPPROTO_ICMPV6   58
#define IPPROTO_NONE     59
#define IPPROTO_DSTOPTS  60
#define IPPROTO_MTP      92
#define IPPROTO_ENCAP    98
#define IPPROTO_PIM      103
#define IPPROTO_COMP     108
#define IPPROTO_SCTP     132
#define IPPROTO_UDPLITE  136
#define IPPROTO_RAW      255
#define IPPROTO_MAX      256

struct ip_mreq {
        struct  in_addr imr_multiaddr;  /* IP multicast address of group */
        struct  in_addr imr_interface;  /* local IP address of interface */
};

struct ip_mreqn {
        struct  in_addr imr_multiaddr;  /* IP multicast address of group */
        struct  in_addr imr_address;    /* local IP address of interface */
        int             imr_ifindex;    /* Interface index; cast to uint32_t */
};

#endif /* OSV__IN_H_ */
