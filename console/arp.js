
var arp_cmd = {
   
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
};


