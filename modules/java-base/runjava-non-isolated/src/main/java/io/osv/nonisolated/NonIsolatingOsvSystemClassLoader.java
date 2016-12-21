package io.osv.nonisolated;

import java.net.URL;
import java.net.URLClassLoader;
import java.util.List;

/**
 * Created by wkozaczuk on 10/5/16.
 */
public class NonIsolatingOsvSystemClassLoader extends URLClassLoader {
    static {
        registerAsParallelCapable();
    }

    public NonIsolatingOsvSystemClassLoader(ClassLoader parent) {
        super(new URL[] {}, parent);
    }

    final void addURLs(List<URL> newURLs) {
        if(null != newURLs) {
            for(URL url: newURLs) {
                super.addURL(url);
            }
        }
    }
}
