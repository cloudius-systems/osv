register_command('md5sum', {
    init: function() {
        this._md5 = new MD5();
    },
    
    md5sum: function(filename) {
       
       var digest = this._md5.md5(cd.pwd() + "/" + filename);
       return (digest)
    },
    
    invoke: function(inp) {
        if (inp.length < 2) {
            this.help();
            return;
        }
        
        var digest = this.md5sum(inp[1]);
        print(digest + "  " + inp[1]);
        
        return (digest);
    },
    
    tab: function(inp) {
        if (inp.length > 2) {
            return ([]);
        }
        
        if ((inp.length == 2) && (completed_word())) {
            return ([]);
        }
            
        return (ls.ls());
    },
    
    help: function() {
        print("md5sum: calculate md5 of file");
        print("usage: md5sum <filename>");
    }
    
})
