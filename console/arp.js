/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */


register_command('arp', {
   
    add: function(ifname, macaddr, ip) {
        networking_interface.arp_add(ifname, macaddr, ip);
    },
        
    invoke: function(inp) {
        if (inp.length != 4) {
            this.help();
            return;
        }
        
        // FIXME: Do error checking...
        this.add(inp[1], inp[2], inp[3])
    },
    
    help: function() {
        print("arp: manipulate the system ARP cache")
        print("usage: arp <ifname> <macaddr> <ip>")
    }
})


