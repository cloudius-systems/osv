/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

importPackage(com.cloudius.cli.tests);

register_command('test', {
        
    init: function() {
        this._test_runner = new TestRunner();
        this._test_runner.registerAllTests();
        this._test_names = this._test_runner.getTestNames();
    },
    
    invoke: function(inp) {
        if (inp.length != 2) {
            print("test: no test specified");
            return;
        }
        
        var testname = inp[1];
        
        if (this._test_names.indexOf(testname) == -1) {
            print("test: not a valid test");
            return;
        }
        
        print(">>> Running test " + testname + "...");
        var beg_time = System.nanoTime();
        var rc = this._test_runner.run(testname);
        var end_time = System.nanoTime();
        
        var dT = end_time - beg_time;
        var sec = dT / 1000000000.0;
        
        System.out.format(">>> Test Duration: %.6fs, %.0fns\n", sec, dT);
        print(">>> Test completed " + (rc ? "successfully!" : "unsuccessfully..."));
        
    },
    
    tab: function(inp) {
        if (inp.length > 2) {
            return ([]);
        }
        
        if ((inp.length == 2) && (completed_word())) {
            return ([]);
        }
        
        return this._test_names;
    },
    
    tab_pretty: function(arg) {
        if (arg.indexOf(".so") == -1) {
            return "\x1B[31m"+arg+"\x1B[0m";
        }
        
        return "\x1B[32m"+arg+"\x1B[0m";
    },
    
    tab_delim: function () {
        return ('\n');
    }
})


