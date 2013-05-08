
var ifconfig_cmd = {
        
    set_ip: function(ifname, ip, netmask) {
        
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
        
        set_ip(ifname, ip, mask);
    },
    
    help: function() {
        print("ifconfig <ifname> <ip> netmask <mask> up")
    }
};


