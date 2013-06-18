
register_command('cd', {
        
    init: function() {
        this._cwd = new File(".");
    },
    
    cwd: function(inp) {
        return (this._cwd);
    },
    
    pwd: function(inp) {
        return (this._cwd.getCanonicalPath());
    },
    
    cd: function(rel_path) {
        // FIXME: Handle absolute paths
        var dir = File(this._cwd, rel_path);
        if ((!dir.exists()) || (!dir.isDirectory())) {
            return -1;
        }
        
        this._cwd = dir;
        return 0;
    },
        
    invoke: function(inp) {
        if (inp.length != 2) {
            return;
        }
        
        var result = this.cd(inp[1]);
        if (result < 0) {
            print("cd: No such file or directory");
            return;
        }
        
        update_prompt();
    },
    
    tab: function(inp) {
        if (inp.length == 1) {
            return (ls.ls_dirs());
        }
        
        if (inp.length > 2) {
            return ([]);
        }
        
        arg = inp[1];
        if (arg.indexOf('/') == -1) {
            return (ls.ls_dirs());
        }
        
        last_slash = arg.lastIndexOf('/');
        dir = arg.substring(0, last_slash);
        
        dirs = ls.ls_dirs(dir);
        arr = [];
        for (var i=0; i<dirs.length; i++) {
            arr[i] = dir + '/' + dirs[i];
        }
        
        return (arr);
    },
    
    tab_pretty: function(arg) {
        last_slash = arg.lastIndexOf('/');
        return (arg.substring(last_slash+1));
    },
    
    tab_final: function(found_match) {
        if (found_match) {
            _line[--_line_idx] = ord('/')
            _line_idx++;
        }
    }
})


