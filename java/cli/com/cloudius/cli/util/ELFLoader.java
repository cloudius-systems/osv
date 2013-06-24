package com.cloudius.cli.util;

import java.io.IOException;

import sun.org.mozilla.javascript.NativeArray;

import com.cloudius.util.Exec;
import com.cloudius.cli.main.RhinoCLI;

public class ELFLoader {

    public boolean run(String[] argv)
    {
        try {
            long exitcode = Exec.run(argv);
            RhinoCLI._scope.put("_exitcode", RhinoCLI._scope, exitcode);
            return true;
        } catch (IOException e) {
            return false;
        }
    }
}
