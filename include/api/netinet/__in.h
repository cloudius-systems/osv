/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */


#ifndef OSV__IN_H_
#define OSV__IN_H_

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
