package io.osv;

import io.osv.jul.IsolatingLogManager;

import java.util.logging.LogManager;

/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
public class ContextIsolator {
    private static final ContextIsolator instance = new ContextIsolator();

    static {
        verifyLogManagerIsInstalled();
    }

    private static void verifyLogManagerIsInstalled() {
        LogManager manager = LogManager.getLogManager();
        if (!(manager instanceof IsolatingLogManager)) {
            throw new AssertionError("For isolation to work logging manager must be "
                    + IsolatingLogManager.class.getName() + " but is: " + manager.getClass().getName());
        }
    }

    private final InheritableThreadLocal<Context> currentContext = new InheritableThreadLocal<>();

    public static ContextIsolator getInstance() {
        return instance;
    }

    public ContextIsolator() {
        currentContext.set(new Context(ClassLoader.getSystemClassLoader()));
    }

    public Context getContext() {
        return currentContext.get();
    }

    public Context run(final ClassLoader classLoader, final SandBoxedProcess process) throws Throwable {
        final Context context = new Context(classLoader);

        Thread thread = new Thread() {
            @Override
            public void run() {
                currentContext.set(context);

                try {
                    process.run();
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                } catch (Throwable throwable) {
                    getUncaughtExceptionHandler().uncaughtException(this, throwable);
                }
            }
        };

        context.setMainThread(thread);
        thread.setUncaughtExceptionHandler(new Thread.UncaughtExceptionHandler() {
            @Override
            public void uncaughtException(Thread t, Throwable e) {
                e.printStackTrace();
            }
        });
        thread.start();
        return context;
    }

    public Context run(SandBoxedProcess process) throws Throwable {
        return run(ClassLoader.getSystemClassLoader(), process);
    }
}
