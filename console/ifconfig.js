
register_command('ifconfig', {
        
    // returns a boolean indicating a successful operation
    set_ip: function(ifname, ip, netmask) {
        var rc = networking_interface.set_ip(ifname, ip, netmask);
        return rc;
    },
    
    if_up: function(ifname) {
        var rc = networking_interface.if_up(ifname);
        return rc;
    },
    
    iflist: function() {
        argv = ["/tools/ifconfig.so"]
        return run_cmd.run(argv)
    },
        
    invoke: function(inp) {
        if (inp.length == 1) {
            return this.iflist();
        }
        
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
        
        rc = this.if_up(ifname);
        if (!rc) {
            print ("ifconfig: unable to ifup interface");
        }
    },
    
    help: function() {
        print("ifconfig <ifname> <ip> netmask <mask> up")
    }
})


