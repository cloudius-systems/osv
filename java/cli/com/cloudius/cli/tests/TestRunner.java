/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

package com.cloudius.cli.tests;

import java.io.File;
import java.util.HashMap;

import sun.org.mozilla.javascript.Context;
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
        this.register("TCPConcurrentDownloads", new TCPConcurrentDownloads());
        this.registerELFTests();
    }
    
    public Scriptable getTestNames() {
        Context cx = Context.enter();
        Object[] names = _tests.keySet().toArray();
        Scriptable tests = cx.newArray(cx.initStandardObjects(), names);
        Context.exit();
        
        return (tests);
    }
}
