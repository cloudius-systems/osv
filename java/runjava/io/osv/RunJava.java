package io.osv;

/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

import java.io.File;
import java.io.FileNotFoundException;
import java.lang.reflect.Constructor;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.net.MalformedURLException;
import java.net.URL;
import java.net.URLClassLoader;
import java.util.ArrayList;
import java.util.List;
import java.util.jar.JarFile;
import java.util.jar.Manifest;
import java.util.zip.ZipException;

public class RunJava {

    public static void main(String[] args) {
        try {
            parseArgs(args);
        } catch (Throwable ex) {
            ex.printStackTrace();
        }
    }

    static void parseArgs(String[] args) throws Throwable {
        ArrayList<String> classpath = new ArrayList<String>();
        for (int i = 0; i < args.length; i++) {
            if (args[i].equals("-jar")) {
                if (i+1 >= args.length) {
                    System.err.println("RunJava: Missing jar name after '-jar'.");
                    return;
                }
                runJar(args[i+1], java.util.Arrays.copyOfRange(args, i+2, args.length), classpath);
                return;
            } else if (args[i].equals("-classpath") || args[i].equals("-cp")) {
                if (i+1 >= args.length) {
                    System.err.println("RunJava: Missing parameter after '"+args[i]+"'");
                    return;
                }
                for (String c : expandClassPath(args[i+1])) {
                    classpath.add(c);
                }
                i++;
            } else if (args[i].startsWith("-D")) {
                int eq = args[i].indexOf('=');
                if (eq<0) {
                    System.err.println("RunJava: Missing '=' in parameter '"+args[i]+"'");
                    return;
                }
                String key = args[i].substring(2, eq);
                String value = args[i].substring(eq+1, args[i].length());
                System.setProperty(key,  value);
            } else if (args[i].equals("-version")) {
                System.err.println("java version \"" +
                        System.getProperty("java.version") + "\"");
                System.err.println(System.getProperty("java.runtime.name") +
                        " (" + System.getProperty("java.runtime.version") +
                        ")");
                System.err.println(System.getProperty("java.vm.name") +
                        " (build " + System.getProperty("java.vm.version") +
                        ", " + System.getProperty("java.vm.info") + ")");
                return;
            } else if (args[i].equals("-Xclassloader")) {
                // Non-standard try - use a different class loader.
                if (i+1 >= args.length) {
                    System.err.println("RunJava: Missing parameter after '"+args[i]+"'");
                    return;
                }
                Xclassloader = args[i+1];
                i++;
            } else if (!args[i].startsWith("-")) {
                runClass(args[i], java.util.Arrays.copyOfRange(args,  i+1,  args.length), classpath);
                return;
            } else {
                System.err.println("RunJava: Unknown parameter '"+args[i]+"'");
                return;
            }
        }
        System.err.println("RunJava: No jar or class specified to run.");
    }

    static void runJar(String jarname, String[] args, ArrayList<String> classpath) throws Throwable {
        File jarfile = new File(jarname);
        try {
            JarFile jar = new JarFile(jarfile);
            Manifest mf = jar.getManifest();
            jar.close();
            String mainClass = mf.getMainAttributes().getValue("Main-Class");
            if (mainClass == null) {
                System.err.println(
                        "RunJava: No 'Main-Class' attribute in manifest of " +
                                jarname);
                return;
            }
            classpath.add(jarname);
            runClass(mainClass, args, classpath);
        } catch (FileNotFoundException e) {
            System.err.println("RunJava: File not found: " + jarname);
        } catch (ZipException e) {
            System.err.println("RunJava: File is not a jar: " + jarname);
        }
    }

    static void runClass(final String mainClass, final String[] args, final Iterable<String> classpath) throws Throwable {
        OsvSystemClassLoader osvClassLoader = getOsvClassLoader();
        ClassLoader appClassLoader = getClassLoader(classpath, osvClassLoader.getParent());

        Context context = ContextIsolator.getInstance().run(appClassLoader,
                new SandBoxedProcess() {

                    @Override
                    public void run() throws Throwable {
                        updateClassPathProperty(classpath);
                        runMain(loadClass(mainClass), args);
                    }
                });

        context.join();
    }

    private static ClassLoader getClassLoader(Iterable<String> classpath, ClassLoader parent) throws MalformedURLException {
        List<URL> urls = toUrls(classpath);

        // If no classpath was specified, don't touch the classloader at
        // all, so we just inherit the one used to run us.
        if (urls.isEmpty()) {
            return parent;
        }

        URL[] urlArray = urls.toArray(new URL[urls.size()]);
        return createAppClassLoader(urlArray, parent);
    }

    private static List<URL> toUrls(Iterable<String> classpath) throws MalformedURLException {
        ArrayList<URL> urls = new ArrayList<URL>();
        for (String path : classpath) {
            urls.add(toUrl(path));
        }
        return urls;
    }

    static void runMain(Class<?> klass, String[] args) throws Throwable {
        Method main = klass.getMethod("main", String[].class);
        try {
            main.invoke(null, new Object[] { args });
        } catch (InvocationTargetException ex) {
            throw ex.getCause();
        }
    }

    private static OsvSystemClassLoader getOsvClassLoader() {
        ClassLoader systemClassLoader = ClassLoader.getSystemClassLoader();
        if (!(systemClassLoader instanceof OsvSystemClassLoader)) {
            throw new AssertionError("System class loader should be an instance of "
                    + OsvSystemClassLoader.class.getName() + " but is "
                    + systemClassLoader.getClass().getName());
        }

        return (OsvSystemClassLoader) systemClassLoader;
    }

    static String Xclassloader = null;
    private static URLClassLoader createAppClassLoader(URL[] urls, ClassLoader parent) {
        if (Xclassloader == null) {
            return new URLClassLoader(urls, parent);
        } else {
            try {
                Class<?> classloader = loadClass(Xclassloader);
                Constructor<?> c = classloader.getConstructor(URL[].class, ClassLoader.class);
                return (URLClassLoader) c.newInstance(urls, parent);
            } catch (Throwable e) {
                e.printStackTrace();
                return new URLClassLoader(urls, parent);
            }
        }
    }

    private static void updateClassPathProperty(Iterable<String> classpath) {
        StringBuilder sb = new StringBuilder();
        boolean first = true;
        for (String path : classpath) {
            if (!first) {
                sb.append(":");
            }
            first = false;
            sb.append(path);
        }
        System.setProperty("java.class.path", sb.toString());
    }

    private static URL toUrl(String path) throws MalformedURLException {
        return new URL("file:///" + path + (isDirectory(path) ? "/" : ""));
    }

    private static boolean isDirectory(String path) {
        return new File(path).isDirectory();
    }

    static Class<?> loadClass(String name) throws ClassNotFoundException {
        return Thread.currentThread().getContextClassLoader().loadClass(name);
    }

    // Expand classpath, as given in the "-classpath" option, to a list of
    // jars or directories. As in the traditional "java" command-line
    // launcher, components of the class path are separated by ":", and
    // we also support the traditional (but awkward) Java wildcard syntax,
    // where "dir/*" adds to the classpath all jar files in the given
    // directory.
    static Iterable<String> expandClassPath(String classpath) {
        ArrayList<String> ret = new ArrayList<String>();
        for (String component : classpath.split(":")) {
            if (component.endsWith("/*")) {
                File dir = new File(
                        component.substring(0,  component.length()-2));
                if (dir.isDirectory()) {
                    for (File file : dir.listFiles()) {
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

}
