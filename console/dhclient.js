register_command('dhclient', {
        
    invoke: function(inp) {
        networking_interface.dhcp_start();
    },
    
    help: function() {
        print("dhclient: dicovers ip and dns\n");
    }
})
