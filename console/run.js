var _global_argv = 0;
var _exitcode = -1;

var run_cmd = {

    init: function() {
        this.elf_loader = new ELFLoader();
    },

    run: function(argv) {
        if (argv.length < 2) {
            return -1;
        }

        argv.splice(0, 1);

        // FIXME: Handle absolute paths
        var f = File(cd.cwd(), argv[0]);
        if (!f.exists()) {
            return -1;
        }

        argv[0] = f.getCanonicalPath();

        _global_argv = argv;
        var success = this.elf_loader.run();
        if (!success) {
            return -1;
        }

        return _exitcode;
    },

    invoke: function(inp) {
        if (inp.length != 2) {
            return;
        }

        rc = this.run(inp);
        if (rc < 0) {
            print("run: couldn't execute elf");
            return;
        }

        print("run: finished with exitcode " + _exitcode);

        return (rc);
    },

    tab: function(inp) {
        return (ls.ls());
    },

};


