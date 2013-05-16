package com.cloudius.cli.util;

import java.io.IOException;

import sun.org.mozilla.javascript.NativeArray;

import com.cloudius.util.Exec;
import com.cloudius.cli.main.RhinoCLI;

public class ELFLoader {

    public boolean run()
    {
        NativeArray js_argv = (NativeArray)RhinoCLI._scope.get("_global_argv",
                RhinoCLI._scope);
        String[] argv = new String[(int)js_argv.getLength()];

        for (int i=0; i < js_argv.getLength(); i++) {
            argv[i] = (String)js_argv.get(i);
        }

        try {
        	long exitcode = Exec.run(argv);
        	RhinoCLI._scope.put("_exitcode", RhinoCLI._scope, exitcode);
        	return true;
        } catch (IOException e) {
        	return false;
        }
    }
}
