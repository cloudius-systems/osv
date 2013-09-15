/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

package com.cloudius.cli.tests;

import java.io.IOException;

import com.cloudius.util.Exec;

public class TestELF implements Test {
    
    private String _elf_path;
    
    public TestELF(String elf_path) {
        _elf_path = elf_path;
    }
    
    public boolean run() {
        // Prepare argv
        String[] argv = new String[1];
        argv[0] = _elf_path;
        
        try {
        	return (Exec.run(argv) == 0);
        } catch(IOException e) {
        	return false;
        }
    }
}