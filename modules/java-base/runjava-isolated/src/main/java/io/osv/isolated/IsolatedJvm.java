package io.osv.isolated;

import io.osv.MainClassNotFoundException;
import io.osv.Jvm;
import io.osv.jul.IsolatingLogManager;
import net.sf.cglib.proxy.Dispatcher;
import net.sf.cglib.proxy.Enhancer;

import java.io.FilePermission;
import java.lang.reflect.Field;
import java.lang.reflect.ReflectPermission;
import java.net.MalformedURLException;
import java.net.URL;
import java.net.URLClassLoader;
import java.security.CodeSource;
import java.security.PermissionCollection;
import java.util.List;
import java.util.Properties;
import java.util.PropertyPermission;
import java.util.logging.LogManager;
import java.util.logging.LoggingPermission;

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

    private Context run(ClassLoader classLoader, final String classpath, final String mainClass,
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

    protected Context runClass(String mainClass, String[] args, Iterable<String> classpath, Properties properties) throws MalformedURLException {
        ClassLoader appClassLoader = createAppClassLoader(classpath, getParentClassLoader());
        return run(appClassLoader, joinClassPath(classpath), mainClass, args, properties);
    }

    private ClassLoader createAppClassLoader(Iterable<String> classpath, ClassLoader parent) throws MalformedURLException {
        List<URL> urls = toUrls(classpath);
        URL[] urlArray = urls.toArray(new URL[urls.size()]);
        return new AppClassLoader(urlArray, parent);
    }

    private ClassLoader getParentClassLoader() {
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

    private IsolatingOsvSystemClassLoader getOsvClassLoader() {
        ClassLoader systemClassLoader = ClassLoader.getSystemClassLoader();
        if (!(systemClassLoader instanceof IsolatingOsvSystemClassLoader)) {
            throw new AssertionError("System class loader should be an instance of "
                    + IsolatingOsvSystemClassLoader.class.getName() + " but is "
                    + systemClassLoader.getClass().getName());
        }

        return (IsolatingOsvSystemClassLoader) systemClassLoader;
    }

    public Object receive() throws InterruptedException {
        return getContext().takeMessage();
    }


    private static class AppClassLoader extends URLClassLoader {
        AppClassLoader(URL[] urlArray, ClassLoader parent) {
            super(urlArray, parent);
        }

        @Override
        protected PermissionCollection getPermissions(CodeSource codesource) {
            PermissionCollection permissions = super.getPermissions(codesource);
            permissions.add(new FilePermission("/java/runjava-isolated.jar", "read"));
            permissions.add(new RuntimePermission("exitVM"));
            permissions.add(new RuntimePermission("shutdownHooks"));
            permissions.add(new RuntimePermission("setContextClassLoader"));
            permissions.add(new ReflectPermission("suppressAccessChecks"));
            permissions.add(new LoggingPermission("control",null));
            permissions.add(new PropertyPermission("java.util.logging.config.*", "read"));
            return permissions;
        }
    }
}
