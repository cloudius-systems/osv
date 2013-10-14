package io.osv;

/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

import java.io.IOException;
import java.net.URL;
import java.util.Enumeration;

public class OsvSystemClassLoader extends ClassLoader {
    private final InheritableThreadLocal<ClassLoader> delegate = new InheritableThreadLocal<ClassLoader>();
    private final ClassLoader defaultSystemClassLoader;

    static {
        registerAsParallelCapable();
    }

    public OsvSystemClassLoader(ClassLoader defaultSystemClassLoader) {
        super(defaultSystemClassLoader);
        this.defaultSystemClassLoader = defaultSystemClassLoader;
    }

    public void run(final ClassLoader classLoader, final SandBoxedProcess process) throws Throwable {
        Thread thread = new Thread() {
            @Override
            public void run() {
                delegate.set(classLoader);

                // We should not register 'this' as context class loader
                // because Class.forName() would cache loaded classes
                // for a shared instance (this) and thus cache for all
                // contexts.
                setContextClassLoader(classLoader);

                try {
                    process.run();
                } catch (Throwable throwable) {
                    getUncaughtExceptionHandler().uncaughtException(this, throwable);
                }
            }
        };

        thread.setUncaughtExceptionHandler(new Thread.UncaughtExceptionHandler() {
            @Override
            public void uncaughtException(Thread t, Throwable e) {
                System.err.println("Uncaught Java exception:");
                e.printStackTrace();
            }
        });

        thread.start();
        thread.join();
    }

    private ClassLoader getDelegate() {
        ClassLoader classLoader = delegate.get();
        if (classLoader != null) {
            return classLoader;
        }

        return defaultSystemClassLoader;
    }

    @Override
    public Class<?> loadClass(String name) throws ClassNotFoundException {
        return getDelegate().loadClass(name);
    }

    @Override
    public URL getResource(String name) {
        return getDelegate().getResource(name);
    }

    @Override
    public Enumeration<URL> getResources(String name) throws IOException {
        return getDelegate().getResources(name);
    }

    public ClassLoader getDefaultSystemClassLoader() {
        return defaultSystemClassLoader;
    }
}
