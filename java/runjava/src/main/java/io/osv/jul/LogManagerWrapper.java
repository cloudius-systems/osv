package io.osv.jul;

import io.osv.Context;
import io.osv.util.LazilyInitialized;
import net.sf.cglib.proxy.Enhancer;
import net.sf.cglib.proxy.MethodInterceptor;
import net.sf.cglib.proxy.MethodProxy;

import java.beans.PropertyChangeEvent;
import java.beans.PropertyChangeListener;
import java.io.IOException;
import java.lang.reflect.Method;
import java.util.concurrent.Callable;
import java.util.logging.Handler;
import java.util.logging.Level;
import java.util.logging.LogManager;
import java.util.logging.Logger;

public class LogManagerWrapper {
    private final LazilyInitialized<LogManager> managerHolder;
    private final Context context;

    private boolean configMayHaveChanged;
    private Logger rootLogger;

    public LogManagerWrapper(final Context context) {
        this.context = context;

        managerHolder = new LazilyInitialized<>(new Callable<LogManager>() {
            @Override
            public LogManager call() throws Exception {
                String clazzName = context.getProperty("java.util.logging.manager");
                if (clazzName == null || clazzName.equals(IsolatingLogManager.class.getName())) {
                    return new DefaultLogManager();
                }

                Class<?> clazz = context.getSystemClassLoader().loadClass(clazzName);
                return (LogManager) clazz.newInstance();
            }
        }, new LogManagerInitializer());
    }

    public LogManager getManager() {
        return managerHolder.get();
    }

    public void reconfigureRootHandlers() {
        LogManager manager = getManager();

        //noinspection SynchronizationOnLocalVariableOrMethodParameter
        synchronized (manager) {
            String handlers = manager.getProperty("handlers");
            if (handlers == null) {
                return;
            }

            for (String handlerName : handlers.split(",")) {
                try {
                    Class clazz = context.getSystemClassLoader().loadClass(handlerName);
                    Handler handler = (Handler) clazz.newInstance();

                    String handlerLevel = manager.getProperty(handlerName + ".level");
                    if (handlerLevel != null) {
                        Level level = Level.parse(handlerLevel);
                        if (level == null) {
                            System.err.println("Failed to parse level: \"" + handlerLevel + "\" for handler " + handlerName);
                        } else {
                            handler.setLevel(level);
                        }
                    }

                    rootLogger.addHandler(handler);
                } catch (Exception e) {
                    System.err.println("Failed to load handler: " + handlerName);
                    e.printStackTrace();
                }
            }
        }
    }

    private class LogManagerInitializer implements LazilyInitialized.Initializer<LogManager> {
        @Override
        public void initialize(final LogManager manager) throws IOException {
            // We need to assign the root logger similarly as is performed in LogManger's static block.
            // The instantiated log manager may or may not use this root logger. In case it does,
            // we must replicate the behavior of LogManager.RootLogger which recreates handlers every
            // time configuration is re-read.
            //
            // However, if this default root logger will not be used we
            // must not attempt to parse the config because it may be of different format. Tomcat
            // for example is using different syntax for "handlers" property. Therefore our root
            // logger must re-read the config only when asked to, lazily.
            rootLogger = createDefaultRootLogger();
            manager.addLogger(rootLogger);

            manager.addPropertyChangeListener(new PropertyChangeListener() {
                @Override
                public void propertyChange(PropertyChangeEvent evt) {
                    synchronized (manager) {
                        configMayHaveChanged = true;
                    }
                }
            });

            manager.readConfiguration();
        }

        private Logger createDefaultRootLogger() {
            Enhancer enhancer = new Enhancer();
            enhancer.setSuperclass(Logger.class);
            enhancer.setCallback(new MethodInterceptor() {
                @Override
                public Object intercept(Object o, Method method, Object[] objects, MethodProxy methodProxy) throws Throwable {
                    synchronized (getManager()) {
                        if (configMayHaveChanged) {
                            configMayHaveChanged = false;
                            reconfigureRootHandlers();
                        }
                    }
                    return methodProxy.invokeSuper(o, objects);
                }
            });
            return (Logger) enhancer.create(new Class[]{String.class, String.class}, new Object[]{"", null});
        }
    }
}
