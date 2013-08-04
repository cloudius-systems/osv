package com.cloudius.cli.main;

import java.io.*;

import com.cloudius.util.IStty;
import com.cloudius.util.Stty;

import sun.org.mozilla.javascript.*;
import sun.org.mozilla.javascript.tools.shell.*;

public class RhinoCLI {
    
    public static void run(TTY tty, boolean init, String[] args) {
        Global global = new Global();
        Context cx = Context.enter();
        try {
            global.init(cx);
            Scriptable scope = ScriptableObject.getTopLevelScope(global);
            
            // Pass some info into the Javascript code as top-level variables:
            scope.put("mainargs", scope, args);
            scope.put("tty", scope, tty);
            scope.put("flagInit", scope, init);

            FileReader cli_js = new FileReader("/console/cli.js");
            cx.evaluateReader(scope, cli_js, "cli.js", 1, null);
            
        } catch (Exception ex) {
            ex.printStackTrace();
        } finally {
            Context.exit();
        }
    }
    
    public static void main(String[] args) {
        run(new ConsoleTTY(), true, args);
    }
    
    public static interface TTY {
        InputStream getIn();
        OutputStream getOut();
        IStty getStty();
    }
    
    public static class ConsoleTTY implements TTY {
        Stty stty = new Stty();
        public InputStream getIn() {
            return System.in;
        }
        public OutputStream getOut() {
            return System.out;
        }
        public IStty getStty() {
            return stty;
        }
    }

}