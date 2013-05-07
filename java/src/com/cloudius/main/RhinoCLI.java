package com.cloudius.main;

import java.io.*;

import com.cloudius.tests.TCPEchoServerTest;
import com.cloudius.util.ELFLoader;

import sun.org.mozilla.javascript.*;
import sun.org.mozilla.javascript.tools.shell.*;

public class RhinoCLI {
    
    public static Global global = new Global();

    public static Scriptable _scope;

    //
    // Invoke the cli.js file take care of exposing all scriptable objects
    // such as the tests
    //
    public static void main(String[] args) {
        Context cx=Context.enter();
        try {
            
            global.init(cx);
            _scope = ScriptableObject.getTopLevelScope(global);
            ScriptableObject.defineClass(_scope, TCPEchoServerTest.class);
            ScriptableObject.defineClass(_scope, ELFLoader.class);
            
            FileReader cli_js = new FileReader("/console/cli.js");
            cx.evaluateReader(_scope, cli_js, "cli.js", 1, null);
            
        } catch (Exception ex) {
            ex.printStackTrace();
        } finally {
            Context.exit();
        }
    }


}