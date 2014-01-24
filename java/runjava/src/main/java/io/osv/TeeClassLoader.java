package io.osv;

import java.io.IOException;
import java.net.URL;
import java.util.Enumeration;

class TeeClassLoader extends ClassLoader {
    private ClassLoader delegate;

    public TeeClassLoader(ClassLoader delegate) {
        super(null);
        this.delegate = delegate;
    }

    @Override
    protected Class<?> findClass(String name) throws ClassNotFoundException {
        return delegate.loadClass(name);
    }

    @Override
    protected URL findResource(String name) {
        return delegate.getResource(name);
    }

    @Override
    protected Enumeration<URL> findResources(String name) throws IOException {
        return delegate.getResources(name);
    }
}
