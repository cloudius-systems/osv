package com.cloudius.cli.main;

import java.io.*;

import sun.org.mozilla.javascript.*;
import sun.org.mozilla.javascript.tools.shell.*;

public class RhinoCLI {
    
    public static void main(String[] args) {
        Global global = new Global();
        Context cx = Context.enter();
        try {
            global.init(cx);
            Scriptable scope = ScriptableObject.getTopLevelScope(global);
            
            // Pass some info into the Javascript code as top-level variables:
            scope.put("mainargs", scope, args);

            FileReader cli_js = new FileReader("/console/cli.js");
            cx.evaluateReader(scope, cli_js, "cli.js", 1, null);
            
        } catch (Exception ex) {
            ex.printStackTrace();
        } finally {
            Context.exit();
        }
    }


}