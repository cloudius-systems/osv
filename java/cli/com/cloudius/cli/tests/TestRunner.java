package com.cloudius.cli.tests;

import java.io.File;
import java.util.HashMap;

import sun.org.mozilla.javascript.Scriptable;

import com.cloudius.cli.main.RhinoCLI;

public class TestRunner {
    
    private HashMap<String, Test> _tests;
    
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
    
    public void registerAllTests() {
        this.register("TCPEchoServerTest", new TCPEchoServerTest());
        this.register("TCPExternalCommunication", new TCPExternalCommunication());
        this.register("TCPDownloadFile", new TCPDownloadFile());
        this.registerELFTests();
    }
    
    public Scriptable getTestNames() {
        Object[] names = _tests.keySet().toArray();
        return (RhinoCLI._cx.newArray(RhinoCLI._scope, names));
    }
}
