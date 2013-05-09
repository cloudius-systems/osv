
var ifconfig_cmd = {
        
    // returns a boolean indicating a successful operation
    set_ip: function(ifname, ip, netmask) {
        var rc = networking_interface.set_ip(ifname, ip, netmask);
        return rc;
    },
        
    invoke: function(inp) {
        if (inp.length != 6) {
            this.help();
            return;
        }
        
        // FIXME: use real argument parsing
        var ifname = inp[1];
        var ip = inp[2];
        var mask = inp[4];
        var up_down_unused = inp[6];
        
        var rc = this.set_ip(ifname, ip, mask);
        if (!rc) {
            print ("ifconfig: unable to set ip, wrong input");
        }
    },
    
    help: function() {
        print("ifconfig <ifname> <ip> netmask <mask> up")
    }
};


