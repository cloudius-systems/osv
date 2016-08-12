package io.osv.isolated;

import io.osv.MainClassNotFoundException;
import io.osv.Jvm;
import io.osv.jul.IsolatingLogManager;
import net.sf.cglib.proxy.Dispatcher;
import net.sf.cglib.proxy.Enhancer;

import java.lang.reflect.Field;
import java.util.Properties;
import java.util.logging.LogManager;

/*
 * Copyright (C) 2016 Waldemar Kozaczuk
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
public class IsolatedJvm extends Jvm<Context> {
    private static final IsolatedJvm instance = new IsolatedJvm();

    static {
        verifyLogManagerIsInstalled();
    }

    private final Context masterContext;
    private final Properties commonSystemProperties;

    private static void verifyLogManagerIsInstalled() {
        LogManager manager = LogManager.getLogManager();
        if (!(manager instanceof IsolatingLogManager)) {
            throw new AssertionError("For isolation to work logging manager must be "
                    + IsolatingLogManager.class.getName() + " but is: " + manager.getClass().getName());
        }
    }

    private final InheritableThreadLocal<Context> currentContext = new InheritableThreadLocal<Context>() {
        @Override
        protected Context initialValue() {
            return masterContext;
        }
    };

    private final ClassLoader parentClassLoaderForIsolates;

    public static IsolatedJvm getInstance() {
        return instance;
    }

    private IsolatedJvm() {
        ClassLoader originalSystemClassLoader = getOsvClassLoader().getParent();
        commonSystemProperties = copyOf(System.getProperties());
        masterContext = new Context(originalSystemClassLoader, copyOf(commonSystemProperties));

        parentClassLoaderForIsolates = originalSystemClassLoader;

        installSystemPropertiesProxy();
    }

    private Properties copyOf(Properties properties) {
        Properties result = new Properties();
        result.putAll(properties);
        return result;
    }

    private void installSystemPropertiesProxy() {
        Enhancer enhancer = new Enhancer();
        enhancer.setSuperclass(Properties.class);
        enhancer.setCallback(new Dispatcher() {
            @Override
            public Object loadObject() throws Exception {
                return instance.getContext().getProperties();
            }
        });
        Properties contextAwareProperties = (Properties) enhancer.create();

        try {
            Field props = System.class.getDeclaredField("props");
            props.setAccessible(true);
            props.set(System.class, contextAwareProperties);
        } catch (NoSuchFieldException | IllegalAccessException e) {
            throw new AssertionError("Unable to override System.props", e);
        }
    }

    public Context getContext() {
        return currentContext.get();
    }

    protected Context run(ClassLoader classLoader, final String classpath, final String mainClass,
                        final String[] args, final Properties properties) {
        Properties contextProperties = new Properties();
        contextProperties.putAll(commonSystemProperties);
        contextProperties.putAll(properties);

        final Context context = new Context(classLoader, contextProperties);

        Thread thread = new Thread() {
            @Override
            public void run() {
                currentContext.set(context);
                context.setProperty("java.class.path", classpath);

                try {
                    runMain(loadClass(mainClass), args);
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                } catch (MainClassNotFoundException e) {
                    context.setException(e);
                } catch (Throwable e) {
                    getUncaughtExceptionHandler().uncaughtException(this, e);
                }
            }
        };

        context.setMainThread(thread);
        thread.setUncaughtExceptionHandler(new Thread.UncaughtExceptionHandler() {
            @Override
            public void uncaughtException(Thread t, Throwable e) {
                context.setException(e);
            }
        });
        thread.setContextClassLoader(classLoader);
        thread.start();
        return context;
    }

    protected ClassLoader getParentClassLoader() {
        return parentClassLoaderForIsolates;
    }

    public void runSync(String... args) throws Throwable {
        Context context = run(args);

        while (true) {
            try {
                context.join();
                return;
            } catch (InterruptedException e) {
                context.interrupt();
            }
        }
    }

    private OsvSystemClassLoader getOsvClassLoader() {
        ClassLoader systemClassLoader = ClassLoader.getSystemClassLoader();
        if (!(systemClassLoader instanceof OsvSystemClassLoader)) {
            throw new AssertionError("System class loader should be an instance of "
                    + OsvSystemClassLoader.class.getName() + " but is "
                    + systemClassLoader.getClass().getName());
        }

        return (OsvSystemClassLoader) systemClassLoader;
    }

    public Object receive() throws InterruptedException {
        return getContext().takeMessage();
    }
}
