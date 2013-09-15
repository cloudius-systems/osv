/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

register_command('run', {

    init: function() {
    },

    run: function(argv) {
        if (argv.length < 1) {
            return -1;
        }

        var f = null;
        if (argv[0].indexOf("/") == 0) {
            f = File(argv[0]);
        } else {
            f = File(cd.cwd(), argv[0]);
        }

        if (!f.exists()) {
            return -1;
        }

        argv[0] = f.getCanonicalPath();

        var elf_loader = new ELFLoader();
        var success = elf_loader.run(argv);
        return [success, elf_loader.lastExitCode()];
    },

    invoke: function(inp) {
        if (inp.length < 2) {
            return;
        }
        
        // ditch 'run'
        inp.splice(0, 1);
        
        rc = this.run(inp);
        if (!rc[0]) {
            print("run: couldn't execute elf");
            return;
        }

        print("run: finished with exitcode " + rc[1]);

        return (rc[1]);
    },

    tab: function(inp) {
        return (ls.ls());
    },

})


