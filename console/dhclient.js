/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

register_command('dhclient', {
        
    invoke: function(inp) {
        networking_interface.dhcp_start();
    },
    
    help: function() {
        print("dhclient: dicovers ip and dns\n");
    }
})
