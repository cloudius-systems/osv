import java.io.File;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.net.MalformedURLException;
import java.net.URL;
import java.net.URLClassLoader;
import java.util.ArrayList;
import java.util.jar.JarFile;
import java.util.jar.Manifest;

public class RunJava {
	
    public static void main(String[] args) {
        try {
            parseArgs(args);
        } catch (Throwable ex) {
            System.err.println("Uncaught Java exception:");
            ex.printStackTrace();
        }
    }

    static void parseArgs(String[] args) throws Throwable {
        for (int i = 0; i < args.length; i++) {
            if (args[i].equals("-jar")) {
                if (i+1 >= args.length) {
                    System.err.println("RunJava: Missing jar name after '-jar'.");
                    return;
                }
                runJar(args[i+1], java.util.Arrays.copyOfRange(args,  i+2, args.length));
                return;
            } else if (args[i].equals("-classpath") || args[i].equals("-cp")) {
                if (i+1 >= args.length) {
                    System.err.println("RunJava: Missing parameter after '"+args[i]+"'");
                    return;
                }
                setClassPath(expandClassPath(args[i+1]));
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
            } else if (!args[i].startsWith("-")) {
                runClass(args[i], java.util.Arrays.copyOfRange(args,  i+1,  args.length));
                return;
            } else {
                System.err.println("RunJava: Unknown parameter '"+args[i]+"'");
                return;
            }
        }
        System.err.println("RunJava: No jar or class specified to run.");
    }

    static void runJar(String jarname, String[] args) throws Throwable {
        File jarfile = new File(jarname);
        JarFile jar = new JarFile(jarfile);
        Manifest mf = jar.getManifest();
        jar.close();
        String mainClass = mf.getMainAttributes().getValue("Main-Class");
        setClassPath(jarname);
        runMain(loadClass(mainClass), args);
    }

    static void runClass(String mainClass, String[] args) throws Throwable {
        runMain(loadClass(mainClass), args);
    }

    static void runMain(Class<?> klass, String[] args) throws Throwable {
        Method main = klass.getMethod("main", String[].class);
        try {
            main.invoke(null, new Object[] { args });
        } catch (InvocationTargetException ex) {
            throw ex.getCause();
        }               
    }

    static void setClassPath(Iterable<String> jars) throws MalformedURLException {
        ArrayList<URL> urls = new ArrayList<URL>();
        for (String jar : jars) {
            urls.add(new URL("file:///" + jar));
        }
        URL[] urlArray = urls.toArray(new URL[urls.size()]);
	    
        URLClassLoader ucl = new URLClassLoader(urlArray,
                ClassLoader.getSystemClassLoader());
        Thread.currentThread().setContextClassLoader(ucl);
        
	    // Also update the java.class.path property
        StringBuilder sb = new StringBuilder();
        boolean first = true;
        for (String jar : jars) {
            if (!first) {
                sb.append(":");
            }
            first = false;
            sb.append(jar);
        }
        System.setProperty("java.class.path", sb.toString());
    }

    static void setClassPath(String jar) throws MalformedURLException {
        setClassPath(java.util.Collections.singleton(jar));
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
