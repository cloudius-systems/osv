
var ls = {
        
    ls_dirs: function(inp) {
        var dir = cd.cwd();
        var filez = dir.listFiles();
        var results = new Array();
        for (var file in filez){
            if (filez[file].isDirectory()) {
                results.push(filez[file].getName());
            }
        }
        
        return (results);
    },
    
    ls: function(inp) {
        var dir = cd.cwd();
        var filez = dir.listFiles();
        var results = new Array();
        for (var file in filez){
            results.push(filez[file].getName());
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

};
