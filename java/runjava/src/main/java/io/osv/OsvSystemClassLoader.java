package io.osv;

/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

import java.io.IOException;
import java.io.InputStream;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.net.URL;
import java.util.Enumeration;

public class OsvSystemClassLoader extends ClassLoader {
    private final ClassLoader defaultSystemClassLoader;

    static {
        registerAsParallelCapable();
    }

    private final Method loadClass;
    private final Method getPackage;
    private final Method getPackages;
    private final Method getClassLoadingLock;
    private final Method findClass;
    private final Method findResource;
    private final Method definePackage;
    private final Method findLibrary;
    private final Method findResources;

    public OsvSystemClassLoader(ClassLoader defaultSystemClassLoader) throws NoSuchMethodException {
        super(defaultSystemClassLoader);
        this.defaultSystemClassLoader = defaultSystemClassLoader;

        loadClass = getProtectedMethod("loadClass", String.class, boolean.class);
        getPackage = getProtectedMethod("getPackage", String.class);
        getPackages = getProtectedMethod("getPackages");
        getClassLoadingLock = getProtectedMethod("getClassLoadingLock", String.class);
        findClass = getProtectedMethod("findClass", String.class);
        findResource = getProtectedMethod("findResource", String.class);
        findResources = getProtectedMethod("findResources", String.class);
        definePackage = getProtectedMethod("definePackage", String.class, String.class, String.class, String.class,
                String.class, String.class, String.class, URL.class);
        findLibrary = getProtectedMethod("findLibrary", String.class);
    }

    private Method getProtectedMethod(String name, Class<?>... parameters) throws NoSuchMethodException {
        Method method = ClassLoader.class.getDeclaredMethod(name, parameters);
        method.setAccessible(true);
        return method;
    }

    private ClassLoader getDelegate() {
        ContextIsolator isolator = ContextIsolator.getInstance();
        ClassLoader classLoader = isolator.getContext().getSystemClassLoader();
        if (classLoader != null && classLoader != this) {
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

    @Override
    protected Class<?> loadClass(String name, boolean resolve) throws ClassNotFoundException {
        return invokeAndForwardException(ClassNotFoundException.class, loadClass, getDelegate(), name, resolve);
    }

    @Override
    protected Package getPackage(String name) {
        return invoke(getPackage, getDelegate(), name);
    }

    @Override
    protected Package[] getPackages() {
        return invoke(getPackages, getDelegate());
    }

    @Override
    protected Object getClassLoadingLock(String className) {
        return invoke(getClassLoadingLock, getDelegate(), className);
    }

    @Override
    protected Class<?> findClass(String name) throws ClassNotFoundException {
        return invokeAndForwardException(ClassNotFoundException.class, findClass, getDelegate(), name);
    }

    @Override
    protected URL findResource(String name) {
        return invoke(findResource, getDelegate(), name);
    }

    @Override
    protected Enumeration<URL> findResources(String name) throws IOException {
        return invokeAndForwardException(IOException.class, findResources, getDelegate(), name);
    }

    @Override
    protected Package definePackage(String name, String specTitle,
                                    String specVersion, String specVendor,
                                    String implTitle, String implVersion,
                                    String implVendor, URL sealBase) throws IllegalArgumentException {
        return invokeAndForwardException(IllegalArgumentException.class,
                definePackage, getDelegate(), name, specTitle, specVersion, specVendor, implTitle,
                implVersion, implVendor, sealBase);
    }

    @Override
    protected String findLibrary(String libname) {
        return invoke(findLibrary, getDelegate(), libname);
    }

    @Override
    public void setDefaultAssertionStatus(boolean enabled) {
        getDelegate().setDefaultAssertionStatus(enabled);
    }

    @Override
    public void setPackageAssertionStatus(String packageName, boolean enabled) {
        getDelegate().setPackageAssertionStatus(packageName, enabled);
    }

    @Override
    public void setClassAssertionStatus(String className, boolean enabled) {
        getDelegate().setClassAssertionStatus(className, enabled);
    }

    @Override
    public void clearAssertionStatus() {
        getDelegate().clearAssertionStatus();
    }

    @Override
    public InputStream getResourceAsStream(String name) {
        return getDelegate().getResourceAsStream(name);
    }

    @SuppressWarnings("unchecked")
    private <T> T invoke(Method method, ClassLoader target, Object... args) {
        try {
            return (T) method.invoke(target, args);
        } catch (IllegalAccessException | InvocationTargetException e) {
            throw new RuntimeException(e);
        }
    }

    @SuppressWarnings("unchecked")
    private <T, E extends Throwable> T invokeAndForwardException(Class<E> exceptionType, Method method,
                                                                 ClassLoader target, Object... args) throws E {
        try {
            return (T) method.invoke(target, args);
        } catch (IllegalAccessException e) {
            throw new RuntimeException(e);
        } catch (InvocationTargetException e) {
            if (exceptionType.isAssignableFrom(e.getCause().getClass())) {
                throw ((E) e.getCause());
            }
            throw new RuntimeException(e);
        }
    }
}
