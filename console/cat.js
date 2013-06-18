
register_command('cat', {
        
    invoke: function(inp) {
        if (inp.length != 2) {
            return;
        }
        
        fd = File(cd.cwd(), inp[1]);
        if (!fd.exists()) {
            print("cat: No such file or directory");
            return;
        }
        
        if (fd.isDirectory()) {
            print("cat: " + inp[1] + ": Is a directory");
            return;
        }
        
        br = new BufferedReader(new FileReader(fd.getCanonicalPath()));
        try {
            line = br.readLine();

            while (line != null) {
                print(line);
                line = br.readLine();
            }
        } finally {
            br.close();
        }
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
})

