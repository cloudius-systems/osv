/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/net_channel.hh>

void net_channel::process_queue()
{
    mbuf* m;
    while (_queue.pop(m)) {
        _process_packet(m);
    }
}


