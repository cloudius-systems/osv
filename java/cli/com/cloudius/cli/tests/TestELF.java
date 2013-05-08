package com.cloudius.cli.tests;

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
        
        boolean success = Exec.run(argv);
        if (!success) {
            return false;
        }
        
        return (Exec._exitcode == 0);
    }
}