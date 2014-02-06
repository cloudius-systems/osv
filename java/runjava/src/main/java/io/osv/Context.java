package io.osv;

import io.osv.jul.LogManagerWrapper;
import io.osv.util.LazilyInitialized;

import java.util.Properties;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.Callable;
import java.util.concurrent.SynchronousQueue;

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

    private final Properties properties;
    private final BlockingQueue<Object> messageQueue = new SynchronousQueue<>();

    private Thread mainThread;
    private Throwable exception;

    public Context(ClassLoader systemClassLoader, Properties properties) {
        this.systemClassLoader = systemClassLoader;
        this.properties = properties;
    }

    public ClassLoader getSystemClassLoader() {
        return systemClassLoader;
    }

    void setMainThread(Thread mainThread) {
        assert this.mainThread == null;
        this.mainThread = mainThread;
    }

    public String getProperty(String key) {
        return properties.getProperty(key);
    }

    public void setProperty(String key, String value) {
        properties.setProperty(key, value);
    }

    public Properties getProperties() {
        return properties;
    }

    public void join() throws InterruptedException, ContextFailedException {
        mainThread.join();
        if (exception != null) {
            throw new ContextFailedException(exception);
        }
    }

    public LogManagerWrapper getLogManagerWrapper() {
        return logManagerWrapper.get();
    }

    public void interrupt() {
        mainThread.interrupt();
    }

    void setException(Throwable exception) {
        this.exception = exception;
    }

    public void send(Object message) throws InterruptedException {
        messageQueue.put(message);
    }

    Object takeMessage() throws InterruptedException {
        return messageQueue.take();
    }
}
