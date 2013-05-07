package com.cloudius.cli.util;

import sun.org.mozilla.javascript.NativeArray;
import sun.org.mozilla.javascript.ScriptableObject;
import sun.org.mozilla.javascript.annotations.JSFunction;

import com.cloudius.util.Exec;
import com.cloudius.cli.main.RhinoCLI;

public class ELFLoader extends ScriptableObject {

    // Identifies the scriptable object
    private static final long serialVersionUID = 87664098764510039L;

    // The native function run() alters this member variable
    public static int _exitcode;

    @JSFunction
    public boolean run()
    {
        NativeArray js_argv = (NativeArray)RhinoCLI._scope.get("_global_argv",
                RhinoCLI._scope);
        String[] argv = new String[(int)js_argv.getLength()];

        for (int i=0; i < js_argv.getLength(); i++) {
            argv[i] = (String)js_argv.get(i);
        }

        boolean success = Exec.run(argv);
        if (success) {
            RhinoCLI._scope.put("_exitcode", RhinoCLI._scope, _exitcode);
        }

        return (success);
    }

    @Override
    public String getClassName() {
        return "ELFLoader";
    }

}
