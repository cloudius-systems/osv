/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */


register_command('ls', {
        
    ls_dirs: function(subdir) {
        var dir;
        if (subdir != undefined) {
            dir = File(cd.cwd(), subdir);
        } else {
            dir = cd.cwd();
        }
        var filez = dir.listFiles();
        var results = new Array();
        for (var file in filez){
            if (filez[file].isDirectory()) {
                results.push(filez[file].getName() + "");
            }
        }
        
        return (results);
    },
    
    ls: function(inp) {
        var dir = cd.cwd();
        var filez = dir.listFiles();
        var results = new Array();
        for (var file in filez){
            results.push(filez[file].getName() + "");
        }
        
        return (results);
    },
    
    invoke: function(inp) {
        filez = this.ls();
        print (filez.join("\t"));
    },
    
    help: function() {
        print("ls: list files in current directory");
    }

})