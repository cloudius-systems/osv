/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */


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


