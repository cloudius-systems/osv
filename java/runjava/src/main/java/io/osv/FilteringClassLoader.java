package io.osv;

class FilteringClassLoader extends ClassLoader {
    private final String allowedPrefix;

    public FilteringClassLoader(ClassLoader parent, String allowedPrefix) {
        super(parent);
        this.allowedPrefix = allowedPrefix;
    }

    private boolean isClassAllowed(String name) {
        return name.startsWith(allowedPrefix);
    }

    @Override
    protected Class<?> loadClass(String name, boolean resolve) throws ClassNotFoundException {
        if (!isClassAllowed(name)) {
            throw new ClassNotFoundException(name);
        }
        return super.loadClass(name, resolve);
    }
}
