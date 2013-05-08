package com.cloudius.cli.main;

import java.io.*;

import com.cloudius.cli.tests.TestRunner;
import com.cloudius.cli.util.ELFLoader;

import sun.org.mozilla.javascript.*;
import sun.org.mozilla.javascript.tools.shell.*;

public class RhinoCLI {
    
    public static Global global = new Global();

    public static Scriptable _scope;
    public static Context _cx;

    //
    // Invoke the cli.js file take care of exposing all scriptable objects
    // such as the tests
    //
    public static void main(String[] args) {
        _cx = Context.enter();
        try {
            
            global.init(_cx);
            _scope = ScriptableObject.getTopLevelScope(global);
            ScriptableObject.defineClass(_scope, TestRunner.class);
            ScriptableObject.defineClass(_scope, ELFLoader.class);
            
            FileReader cli_js = new FileReader("/console/cli.js");
            _cx.evaluateReader(_scope, cli_js, "cli.js", 1, null);
            
        } catch (Exception ex) {
            ex.printStackTrace();
        } finally {
            Context.exit();
        }
    }


}