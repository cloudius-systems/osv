var route_options = [
  {opt:"add", op:"bool", help:"add route"},
  {opt:"del", op:"bool", help:"del route"},
  {opt:"deafult", op:"bool", help:"deafult route 0.0.0.0"},
  {opt:"-net", op:"store", metaname: "NETWORK", help:"target is a network"},
  {opt:"-host", op:"store", metaname: "HOST", help:"target is a host"},
  {opt:"gw", op:"store", metaname: "GW", help:"set default route"},
  {opt:"netmask", op:"store", metaname: "NETMASK", help:"for a network route"}];

var route_cmd = {
        
    add_deafult_gw: function(gwaddr) {
        networking_interface.route_add_default(gwaddr);
    },
        
    init: function() {
        this._parser = new OptParser(route_options);
    },
    
    invoke: function(inp) {
        opts = this._parser.parse(inp);
        if (opts.err) {
            this.help();
            print ("error: " + opts.err);
            return;
        }
        
        // add default gw
        if ((opts.add == true) && (opts.gw != false) && (opts.default != false)) {
            this.add_deafult_gw(opts.gw);
            return;
        }
        
        this.help();
    },
    
    tab: function(inp) {
        return (this._parser.names);
    },
    
    help: function() {
        print("route: manipulate/display the routing table\n");
        this._parser.printUsage();
    }
}
