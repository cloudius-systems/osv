/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

package com.cloudius.trace;

import com.cloudius.*;
import java.util.*;

public class Tracepoint {

    static {
        Config.loadJNI("tracepoint.so");
    }

    private String name;
    private long handle;

    Tracepoint(long handle) {
        this.handle = handle;
    }

    public Tracepoint(String name) {
        this.handle = findByName(name);
    }

    public String getName() {
        return doGetName(handle);
    }

    public void enable() {
        doEnable(handle);
    }

    public static List<Tracepoint> list() {
        long[] handles = doList();
        System.out.flush();
        List<Tracepoint> ret = new ArrayList<Tracepoint>(handles.length);
        for (long handle : handles) {
            ret.add(new Tracepoint(handle));
        }
        return ret;
    }

    long createCounter() {
        return doCreateCounter(handle);
    }

    native static long[] doList();

    native static long findByName(String name);

    native static void doEnable(long handle);

    native static String doGetName(long handle);

    native static long doCreateCounter(long handle);

    native static void destroyCounter(long handle);

    native static long readCounter(long handle);

}
