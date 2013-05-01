package com.cloudius.main;

import java.io.*;

import com.cloudius.tests.TCPEchoServerTest;

import sun.org.mozilla.javascript.*;
import sun.org.mozilla.javascript.tools.shell.*;

public class RhinoCLI {
    
    public static Global global = new Global();

    //
    // Invoke the cli.js file take care of exposing all scriptable objects
    // such as the tests
    //
    public static void main(String[] args) {
        Context cx=Context.enter();
        try {
            
            global.init(cx);
            Scriptable scope = ScriptableObject.getTopLevelScope(global);
            ScriptableObject.defineClass(scope, TCPEchoServerTest.class);
            
            FileReader cli_js = new FileReader("/console/cli.js");
            cx.evaluateReader(scope, cli_js, "cli.js", 1, null);
            
        } catch (Exception ex) {
            ex.printStackTrace();
        } finally {
            Context.exit();
        }
    }


}