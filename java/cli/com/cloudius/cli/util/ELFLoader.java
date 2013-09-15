/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

package com.cloudius.cli.util;

import java.io.IOException;

import com.cloudius.util.Exec;

public class ELFLoader {
    
    long exitcode;

    public boolean run(String[] argv)
    {
        try {
            exitcode = Exec.run(argv);
            return true;
        } catch (IOException e) {
            return false;
        }
    }
    
    public long lastExitCode() {
        return exitcode;
    }
}
