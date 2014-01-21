/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

package com.cloudius.util;

import java.io.IOException;

import com.cloudius.Config;

public class Exec {

    static {
        Config.loadJNI("elf-loader.so");
    }
    
    /**
     * Executes the specified command and arguments in the current thread,
     * waits for it to complete, and returns its return value.
     * 
     * <code>argv[0]</code> names an ELF object, whose <code>int main(int argc,
     * char **argv)</code> function is run.
     * 
     * Unlike the UNIX conventions (see wait(2) and system(3)), here the
     * return value from main() can be any 32-bit integer, and is not limited
     * to 8-bit nonnegative numbers. Failure to execute command is signalled
     * by throwing an IOException.  
     */
    public native static int run(String[] argv) throws IOException;

}
