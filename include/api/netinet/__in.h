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

#define IN_CLASSA(a)            ((((in_addr_t)(a)) & 0x80000000) == 0)
#define IN_CLASSA_NET           0xff000000
#define IN_CLASSA_NSHIFT        24
#define IN_CLASSA_HOST          (0xffffffff & ~IN_CLASSA_NET)
#define IN_CLASSA_MAX           128
#define IN_CLASSB(a)            ((((in_addr_t)(a)) & 0xc0000000) == 0x80000000)
#define IN_CLASSB_NET           0xffff0000
#define IN_CLASSB_NSHIFT        16
#define IN_CLASSB_HOST          (0xffffffff & ~IN_CLASSB_NET)
#define IN_CLASSB_MAX           65536
#define IN_CLASSC(a)            ((((in_addr_t)(a)) & 0xe0000000) == 0xc0000000)
#define IN_CLASSC_NET           0xffffff00
#define IN_CLASSC_NSHIFT        8
#define IN_CLASSC_HOST          (0xffffffff & ~IN_CLASSC_NET)
#define IN_CLASSD(a)            ((((in_addr_t)(a)) & 0xf0000000) == 0xe0000000)
#define IN_MULTICAST(a)         IN_CLASSD(a)
#define IN_EXPERIMENTAL(a)      ((((in_addr_t)(a)) & 0xe0000000) == 0xe0000000)
#define IN_BADCLASS(a)          ((((in_addr_t)(a)) & 0xf0000000) == 0xf0000000)

#define IP_TOS             1
#define IP_TTL             2
#define IP_HDRINCL         3
#define IP_OPTIONS         4
#define IP_ROUTER_ALERT    5
#define IP_RECVOPTS        6
#define IP_RETOPTS         7
#define IP_PKTINFO         8
#define IP_PKTOPTIONS      9
#define IP_PMTUDISC        10
#define IP_MTU_DISCOVER    10
#define IP_RECVERR         11
#define IP_RECVTTL         12
#define IP_RECVTOS         13
#define IP_MTU             14
#define IP_FREEBIND        15
#define IP_IPSEC_POLICY    16
#define IP_XFRM_POLICY     17
#define IP_PASSSEC         18
#define IP_TRANSPARENT     19
#define IP_ORIGDSTADDR     20
#define IP_RECVORIGDSTADDR IP_ORIGDSTADDR
#define IP_MINTTL          21
#define IP_MULTICAST_IF    32
#define IP_MULTICAST_TTL   33
#define IP_MULTICAST_LOOP  34
#define IP_ADD_MEMBERSHIP  35
#define IP_DROP_MEMBERSHIP 36
#define IP_UNBLOCK_SOURCE  37
#define IP_BLOCK_SOURCE    38
#define IP_ADD_SOURCE_MEMBERSHIP  39
#define IP_DROP_SOURCE_MEMBERSHIP 40
#define IP_MSFILTER        41

#define IP_RECVRETOPTS IP_RETOPTS

#ifdef _GNU_SOURCE
#define MCAST_JOIN_GROUP   42
#define MCAST_BLOCK_SOURCE 43
#define MCAST_UNBLOCK_SOURCE      44
#define MCAST_LEAVE_GROUP  45
#define MCAST_JOIN_SOURCE_GROUP   46
#define MCAST_LEAVE_SOURCE_GROUP  47
#define MCAST_MSFILTER     48

#define IP_MULTICAST_ALL   49
#define IP_UNICAST_IF      50

#define MCAST_EXCLUDE 0
#define MCAST_INCLUDE 1
#endif



#define INADDR_ANY        ((in_addr_t) 0x00000000)
#define INADDR_BROADCAST  ((in_addr_t) 0xffffffff)
#define INADDR_NONE       ((in_addr_t) 0xffffffff)
#define INADDR_LOOPBACK   ((in_addr_t) 0x7f000001)

#define IP_MAX_MEMBERSHIPS              20

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
