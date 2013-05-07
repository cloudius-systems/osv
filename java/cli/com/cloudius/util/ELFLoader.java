package com.cloudius.util;

import java.util.*;

import sun.org.mozilla.javascript.NativeArray;
import sun.org.mozilla.javascript.NativeGlobal;
import sun.org.mozilla.javascript.ScriptableObject;
import sun.org.mozilla.javascript.annotations.JSFunction;

import com.cloudius.main.RhinoCLI;

public class ELFLoader extends ScriptableObject {

    // Identifies the scriptable object
    private static final long serialVersionUID = 87664098764510039L;

    static {
        System.load("/usr/lib/jni/elf-loader.so");
    }

    public native static boolean run(String[] argv);

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

        boolean success = run(argv);
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
