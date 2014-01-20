package io.osv;

/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
public final class Context {
    private final ClassLoader systemClassLoader;
    private Thread mainThread;

    public Context(ClassLoader systemClassLoader) {
        this.systemClassLoader = systemClassLoader;
    }

    public ClassLoader getSystemClassLoader() {
        return systemClassLoader;
    }

    void setMainThread(Thread mainThread) {
        assert this.mainThread == null;
        this.mainThread = mainThread;
    }

    public String getProperty(String key) {
        return System.getProperty(key); // TODO: isolate properties
    }

    public void join() throws InterruptedException {
        mainThread.join();
    }
}
