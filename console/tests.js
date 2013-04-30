importPackage(Packages.com.cloudius.tests);

var unit_tests = new Array();
unit_tests["TCPEchoServerTest"] = TCPEchoServerTest;


var test_cmd = {
        
    init: function() {
        this._test_names = new Array();
        for (var test in unit_tests) {
            this._test_names.push(test);
        }
    },
        
    invoke: function(inp) {
        if (inp.length != 2) {
            print("test: no test specified");
            return;
        }
        
        var classname = unit_tests[inp[1]];
        var t = new classname();
        
        print(">>> Running test " + inp[1] + "...");
        var rc = t.run();
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
};


