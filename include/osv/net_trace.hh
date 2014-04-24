/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef NET_TRACE_HH
#define NET_TRACE_HH

#include <bsd/sys/sys/mbuf.h>
#include <bsd/sys/net/if.h>

void log_eth_packet(struct ifnet *ifp, struct mbuf *m);
void log_loopback_packet(struct mbuf *m);

#endif
