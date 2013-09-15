/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */


register_command('pwd', {
        
    pwd: function() {
        return (cd.pwd());
    },
        
    invoke: function(inp) {
        print (this.pwd());
    },
    
})


