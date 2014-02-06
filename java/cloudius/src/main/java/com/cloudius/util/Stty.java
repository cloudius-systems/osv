/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

package com.cloudius.util;

import com.cloudius.Config;

public class Stty implements IStty {
    static {
        Config.loadJNI("stty.so");
    }

    private long savedStateAddr;

    private native long saveState();
    private native void freeState(long addr);

    public Stty() {
        savedStateAddr = saveState();
    }

    protected void finalize() throws Throwable {
        close();
    }

    public native void raw();
    private native void reset(long addr);
    public void reset() {
        reset(savedStateAddr);
    }
    public void close() throws Exception {
        reset(savedStateAddr);
        freeState(savedStateAddr);
        savedStateAddr = 0;
    }

    public native void jlineMode();
}
