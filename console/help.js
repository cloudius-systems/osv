
register_command('help', {
        
    invoke: function(inp) {
        if (inp.length != 2) {
            return;
        }
        
        if (inp[1] in _commands) {
            var cmd = _commands[inp[1]];
            if (cmd.help) {
                cmd.help();
            } else {
                print("No help for '" + inp[1] + "'...");
            }
        };
    },
    
    tab: function(inp) {
        return (_command_names);
    },
    
    help: function() {
        print("help: describe command");
    }
})


