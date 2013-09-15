/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

register_command('java', {
    invoke: function(argv) {
        argv.shift();  // Remove the command name ("java")
        Packages.RunJava.main(argv);
        return 0;
    },
})
