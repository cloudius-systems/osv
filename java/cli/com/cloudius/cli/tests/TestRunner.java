package com.cloudius.cli.tests;

import java.io.File;
import java.util.HashMap;

import com.cloudius.cli.main.RhinoCLI;

import sun.org.mozilla.javascript.Scriptable;
import sun.org.mozilla.javascript.ScriptableObject;
import sun.org.mozilla.javascript.annotations.*;

public class TestRunner extends ScriptableObject {
    
    private static final long serialVersionUID = 555837467364642L;

    @Override
    public String getClassName() {
        return "TestRunner";
    }
    
    private HashMap<String, Test> _tests;
    
    @JSConstructor
    public TestRunner() {
        _tests = new HashMap<String, Test>();
    }
    
    public boolean register(String name, Test test) {
        if (_tests.containsKey(name)) {
            return false;
        }
        
        _tests.put(name, test);
        return true;
    }
    

    @JSFunction
    public boolean run(String name) {
        if (!_tests.containsKey(name)) {
            return false;
        }
        
        Test tst = _tests.get(name);
        boolean rc = tst.run();
        
        return rc;
    }
    
    public void registerELFTests() {
        File dir = new File("/tests");
        File[] files = dir.listFiles();
        for (File f: files) {
            try {
                if (f.getName().contains(".so")) {
                    this.register(f.getName(), 
                            new TestELF(f.getCanonicalPath().toString()));
                }
            } catch (Exception ex) {
                // Do nothing
            }
        }
    }
    
    @JSFunction
    public void registerAllTests() {
        this.register("TCPEchoServerTest", new TCPEchoServerTest());
        this.register("TCPExternalCommunication", new TCPExternalCommunication());
        this.register("TCPDownloadFile", new TCPDownloadFile());
        this.registerELFTests();
    }
    
    @JSFunction
    public Scriptable getTestNames() {
        Object[] names = _tests.keySet().toArray();
        return (RhinoCLI._cx.newArray(RhinoCLI._scope, names));
    }
}