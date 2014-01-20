package io.osv;

/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
public class ContextIsolator {
    private static final ContextIsolator instance = new ContextIsolator();

    private final InheritableThreadLocal<Context> currentContext = new InheritableThreadLocal<>();

    public static ContextIsolator getInstance() {
        return instance;
    }

    public ContextIsolator() {
        Context mainContext = new Context(ClassLoader.getSystemClassLoader());
        currentContext.set(mainContext);
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
