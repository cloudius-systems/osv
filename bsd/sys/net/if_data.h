/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
#ifndef _NET_IF_DATA_H
#define _NET_IF_DATA_H

#include <bits/alltypes.h>
#include <api/sys/types.h>

/*
 * Structure describing information about an interface
 * which may be of interest to management entities.
 */
struct if_data {
    /* generic interface information */
    u_char  ifi_type;       /* ethernet, tokenring, etc */
    u_char  ifi_physical;   /* e.g., AUI, Thinnet, 10base-T, etc */
    u_char  ifi_addrlen;    /* media address length */
    u_char  ifi_hdrlen;     /* media header length */
    u_char  ifi_link_state; /* current link state */
    u_char  ifi_spare_char1;/* spare byte */
    u_char  ifi_spare_char2;/* spare byte */
    u_char  ifi_spare_char3;/* spare byte */
    u_long  ifi_datalen;    /* length of this data struct */
    u_long  ifi_mtu;        /* maximum transmission unit */
    u_long  ifi_metric;     /* routing metric (external only) */
    u_long  ifi_baudrate;   /* linespeed */
    /* volile statistics */
    u_long  ifi_ipackets;   /* packets received on interface */
    u_long  ifi_ierrors;    /* input errors on interface */
    u_long  ifi_opackets;   /* packets sent on interface */
    u_long  ifi_oerrors;    /* output errors on interface */
    u_long  ifi_collisions; /* collisions on csma interfaces */
    u_long  ifi_ibytes;     /* total number of octets received */
    u_long  ifi_obytes;     /* total number of octets sent */
    u_long  ifi_imcasts;    /* packets received via multicast */
    u_long  ifi_omcasts;    /* packets sent via multicast */
    u_long  ifi_iqdrops;    /* dropped on input, this interface */
    u_long  ifi_noproto;    /* destined for unsupported protocol */
    u_long  ifi_hwassist;   /* HW offload capabilities, see IFCAP */
    time_t  ifi_epoch;      /* uptime at attach or stat reset */
    struct timeval ifi_lastchange;/* time of last administrative change */
    u_long  ifi_ibh_wakeups;/* number times Rx BH has been woken up */
    u_long  ifi_oworker_kicks;/* number of kicks from Tx worker */
    u_long  ifi_oworker_wakeups;/* number times Tx worker has been woken up */
    u_long  ifi_oworker_packets;/* number of Tx packets handled by a Tx worker */
    u_long  ifi_okicks;     /* total number of Tx kicks */
    u_long  ifi_oqueue_is_full;/* number of times the packet could not
                                * be sent due to a lack of free space
                                * on a HW ring
                                */
};


#endif /* _NET_IF_DATA_H */
