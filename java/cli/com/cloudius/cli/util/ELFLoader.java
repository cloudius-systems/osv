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
