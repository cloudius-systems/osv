package io.osv;

import io.osv.jul.LogManagerWrapper;
import io.osv.util.LazilyInitialized;

import java.util.concurrent.Callable;

/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
public final class Context {
    private final ClassLoader systemClassLoader;

    private final LazilyInitialized<LogManagerWrapper> logManagerWrapper = new LazilyInitialized<>(
            new Callable<LogManagerWrapper>() {
                @Override
                public LogManagerWrapper call() throws Exception {
                    return new LogManagerWrapper(Context.this);
                }
            });

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

    public LogManagerWrapper getLogManagerWrapper() {
        return logManagerWrapper.get();
    }

    public void interrupt() {
        mainThread.interrupt();
    }
}
