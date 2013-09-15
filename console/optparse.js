/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

//
// Parsed options
// 
// Description: being returned as an object created by the OptParser
//
function ParsedOptions () {
}

ParsedOptions.prototype._setTrue = function (attr) {
    Object.defineProperty(this, attr, {value : true});
}

ParsedOptions.prototype._setFalse = function (attr) {
    Object.defineProperty(this, attr, {value : false});
}

ParsedOptions.prototype._store = function (attr, a_value) {
    Object.defineProperty(this, attr, {value : a_value});
}

//
// OptParser
// 
// Description: parse command line out of metadata options
//
function OptParser(options) {
    this.options = options;
    
    this.names = new Array();
    
    for (var i=0; i < options.length; i++) {
        this.names.push(options[i].opt);
    }
}

OptParser.prototype.parse = function(commandline) {
    
    var parsed = new ParsedOptions();
    
    // TODO: Search for unrecognized options and set "err"
    
    // Parse options
    for (var i=0; i < this.options.length; i++) {
        var opt = this.options[i];
        var cmd_id = this._findIdx(opt.opt, commandline);
        var optname = opt.opt;

        // trim '-'
        optname = optname.replace(/^-+/,"");
        
        if (cmd_id == -1) {
            parsed._setFalse(optname);
            continue;
        }
        
        if (opt.op == "bool") {
            parsed._setTrue(optname)
        }
        
        if (opt.op == "store") {
            if ((cmd_id+1 >= commandline.length) || 
                (this._isOption(commandline[cmd_id+1]))) {
                parsed._store("err", "option " + opt.opt + " must name a value")
                return (parsed);
            }
            
            parsed._store(optname, commandline[cmd_id+1]);
        }
    }
    
    return (parsed);
}

OptParser.prototype._isOption = function(arg) {
    for (var i=0; i < this.names.length; i++) {
        if (this.names[i] == arg) {
            return (true);
        }
    }
    
    return (false);
}

// if val is found in arr, return the id, else return -1
OptParser.prototype._findIdx = function(val, arr) {
    for (var i=0; i < arr.length; i++) {
        if (arr[i] == val) {
            return (i);
        }
    }
    
    return (-1);
}

OptParser.prototype.printUsage = function() {
    print("Options:");
    for (var i=0; i < this.options.length; i++) {
        var opt = this.options[i];
        var out_opt = opt.opt;
        if (opt.op == "store") {
            if (opt.metaname) {
                out_opt += " " + opt.metaname;
            } else {
                out_opt += " <arg>";
            }
        }
        var line = System.out.format("    %-20s    %s\n", out_opt, opt.help);
    }
}
