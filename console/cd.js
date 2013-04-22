
var cd = {
        
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
        return (ls.ls_dirs());
    },
    
    tab_final: function(found_match) {
        if (found_match) {
            _line[--_line_idx] = ord('/')
            _line_idx++;
        }
    }
};


