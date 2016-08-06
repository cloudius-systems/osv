package io.osv;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.FilePermission;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.net.MalformedURLException;
import java.net.URL;
import java.net.URLClassLoader;
import java.security.CodeSource;
import java.security.PermissionCollection;
import java.util.ArrayList;
import java.util.List;
import java.util.Properties;
import java.util.jar.JarFile;
import java.util.jar.Manifest;
import java.util.zip.ZipException;

/**
 * Created by wkozaczuk on 7/17/16.
 */
public abstract class JvmApp<T> {
    public T run(String... args) throws Throwable {
        Properties properties = new Properties();

        ArrayList<String> classpath = new ArrayList<>();
        for (int i = 0; i < args.length; i++) {
            if (args[i].equals("-jar")) {
                if (i + 1 >= args.length) {
                    throw new IllegalArgumentException("Missing jar name after '-jar'.");
                }
                return runJar(args[i + 1], java.util.Arrays.copyOfRange(args, i + 2, args.length), classpath, properties);
            } else if (args[i].equals("-classpath") || args[i].equals("-cp")) {
                if (i + 1 >= args.length) {
                    throw new IllegalArgumentException("Missing parameter after '" + args[i] + "'");
                }
                for (String c : expandClassPath(args[i + 1])) {
                    classpath.add(c);
                }
                i++;
            } else if (args[i].startsWith("-D")) {
                int eq = args[i].indexOf('=');
                if (eq < 0) {
                    /* -Dfoo is a special case for -Dfoo=true */
                    String key = args[i].substring(2);
                    properties.put(key, "true");
                } else {
                    String key = args[i].substring(2, eq);
                    String value = args[i].substring(eq + 1, args[i].length());
                    properties.put(key, value);
                }
            } else if (!args[i].startsWith("-")) {
                return runClass(args[i], java.util.Arrays.copyOfRange(args, i + 1, args.length), classpath, properties);
            } else {
                throw new IllegalArgumentException("Unknown parameter '" + args[i] + "'");
            }
        }
        throw new IllegalArgumentException("No jar or class specified to run.");
    }

    private T runJar(String jarName, String[] args, ArrayList<String> classpath, Properties properties) throws Throwable {
        File jarFile = new File(jarName);
        try {
            JarFile jar = new JarFile(jarFile);
            Manifest mf = jar.getManifest();
            jar.close();
            String mainClass = mf.getMainAttributes().getValue("Main-Class");
            if (mainClass == null) {
                throw new IllegalArgumentException("No 'Main-Class' attribute in manifest of " + jarName);
            }
            classpath.add(jarName);
            return runClass(mainClass, args, classpath, properties);
        } catch (FileNotFoundException e) {
            throw new IllegalArgumentException("File not found: " + jarName);
        } catch (ZipException e) {
            throw new IllegalArgumentException("File is not a jar: " + jarName, e);
        }
    }

    private T runClass(String mainClass, String[] args, Iterable<String> classpath, Properties properties) throws MalformedURLException {
        ClassLoader appClassLoader = createAppClassLoader(classpath, getParentClassLoader());
        return run(appClassLoader, joinClassPath(classpath), mainClass, args, properties);
    }

    protected abstract T run(ClassLoader classLoader, final String classpath, final String mainClass,
                final String[] args, final Properties properties);

    protected abstract ClassLoader getParentClassLoader();

    private ClassLoader createAppClassLoader(Iterable<String> classpath, ClassLoader parent) throws MalformedURLException {
        List<URL> urls = toUrls(classpath);
        URL[] urlArray = urls.toArray(new URL[urls.size()]);
        return new AppClassLoader(urlArray, parent);
    }

    private List<URL> toUrls(Iterable<String> classpath) throws MalformedURLException {
        ArrayList<URL> urls = new ArrayList<>();
        for (String path : classpath) {
            urls.add(toUrl(path));
        }
        return urls;
    }

    protected void runMain(Class<?> klass, String[] args) throws Throwable {
        Method main = klass.getMethod("main", String[].class);
        try {
            main.invoke(null, new Object[]{args});
        } catch (InvocationTargetException ex) {
            throw ex.getCause();
        }
    }

    protected Class<?> loadClass(String name) throws MainClassNotFoundException {
        try {
            return Thread.currentThread().getContextClassLoader().loadClass(name);
        } catch (ClassNotFoundException ex) {
            throw new MainClassNotFoundException(name);
        }
    }

    private String joinClassPath(Iterable<String> classpath) {
        StringBuilder sb = new StringBuilder();
        boolean first = true;
        for (String path : classpath) {
            if (!first) {
                sb.append(":");
            }
            first = false;
            sb.append(path);
        }
        return sb.toString();
    }

    private URL toUrl(String path) throws MalformedURLException {
        return new URL("file:///" + path + (isDirectory(path) ? "/" : ""));
    }

    private boolean isDirectory(String path) {
        return new File(path).isDirectory();
    }

    // Expand classpath, as given in the "-classpath" option, to a list of
    // jars or directories. As in the traditional "java" command-line
    // launcher, components of the class path are separated by ":", and
    // we also support the traditional (but awkward) Java wildcard syntax,
    // where "dir/*" adds to the classpath all jar files in the given
    // directory.
    private Iterable<String> expandClassPath(String classpath) {
        ArrayList<String> ret = new ArrayList<>();
        for (String component : classpath.split(":")) {
            if (component.endsWith("/*")) {
                File dir = new File(
                        component.substring(0, component.length() - 2));
                if (dir.isDirectory()) {
                    File[] files = dir.listFiles();
                    if (files == null) {
                        continue;
                    }
                    for (File file : files) {
                        String filename = file.getPath();
                        if (filename.endsWith(".jar")) {
                            ret.add(filename);
                        }
                    }
                    continue; // handled this path component
                }
            }
            ret.add(component);
        }
        return ret;
    }

    private static class AppClassLoader extends URLClassLoader {
        public AppClassLoader(URL[] urlArray, ClassLoader parent) {
            super(urlArray, parent);
        }

        @Override
        protected PermissionCollection getPermissions(CodeSource codesource) {
            PermissionCollection permissions = super.getPermissions(codesource);
            permissions.add(new FilePermission("/usr/lib/jvm/jre/lib/ext/runjava.jar", "read"));
            permissions.add(new RuntimePermission("exitVM"));
            return permissions;
        }
    }
}
