package io.osv.util;

import java.net.URL;
import java.net.URLClassLoader;
import java.security.CodeSource;
import java.security.ProtectionDomain;

/*
 * Copyright (C) 2016 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

/**
 * Utility class for class diagnostic purposes, to analyze the
 * ClassLoader hierarchy and security information for any given class.
 *
 * @author Waldemar Kozaczuk
 * @see java.lang.ClassLoader
 */
public class ClassDiagnostics {

    public static final String JAVA_DIAGNOSTICS_PROPERTY_NAME = "osv.java.diagnostics";

    public static boolean showDiagnostics(String args[]) {
        if (null != System.getProperty(JAVA_DIAGNOSTICS_PROPERTY_NAME)) {
            return true;
        }
        if (args == null) {
            return false;
        }
        final String diagnosticsPropertyToCompare = ("-D" + JAVA_DIAGNOSTICS_PROPERTY_NAME).toLowerCase();
        for (String arg : args) {
            if (null != arg && arg.toLowerCase().startsWith(diagnosticsPropertyToCompare)) {
                return true;
            }
        }
        return false;
    }

    /**
     * Show security information in terms of permissions granted for the given class.
     *
     * @param clazz class to analyze security for
     * @return a String showing the security information about class as well which code source (typically jar)
     * it has been loaded from
     */
    public static String showClassSecurity(final Class clazz) {
        if (null != clazz) {
            final ProtectionDomain protectionDomain = clazz.getProtectionDomain();
            final CodeSource codeSource = protectionDomain.getCodeSource();
            return "class=[" + clazz.getName() + "] loaded from\n" +
                    "\tcode source=[" + (codeSource != null ? protectionDomain.getCodeSource().getLocation() : "BOOTSTRAP code source (rt.jar)") + "]\n" +
                    "\twith permissions=[" + protectionDomain.getPermissions() + "]";
        } else
            throw new IllegalArgumentException("Received null clazz argument");
    }

    /**
     * Show the class loader hierarchy for the given class.
     *
     * @param clazz class to analyze hierarchy for
     * @return a String showing the class loader hierarchy for this class
     */
    public static String showClassLoaderHierarchy(final Class clazz, final boolean showContextClassLoader) {
        if (null != clazz) {
            if (showContextClassLoader) {
                final ClassLoader contextClassLoader = Thread.currentThread().getContextClassLoader();

                return "context class loader=[" + buildClassLoaderInfo(contextClassLoader, "\t") + "] hashCode=" + contextClassLoader.hashCode() + "\n" +
                        "class=[" + clazz.getName() + "] loaded by " +
                        buildClassLoaderHierarchyInfo(clazz.getClassLoader(), "\n", "\t", 0);
            } else {
                return "class=[" + clazz.getName() + "] loaded by " +
                        buildClassLoaderHierarchyInfo(clazz.getClassLoader(), "\n", "\t", 0);
            }
        } else
            throw new IllegalArgumentException("Received null clazz argument");
    }

    private static String buildClassLoaderHierarchyInfo(ClassLoader classLoader, String lineBreak, String tabText, int indent) {
        final StringBuilder builder = new StringBuilder();
        for (int i = 0; i < indent; i++) {
            builder.append(tabText);
        }

        if (classLoader == null) {
            builder.append("[BOOTSTRAP classloader]").append(lineBreak);
        } else {
            final String urlIndent = builder.toString() + tabText;
            builder.append("[").append(buildClassLoaderInfo(classLoader, urlIndent)).
                    append("] hashCode=").
                    append(classLoader.hashCode()).
                    append(" which is PARENT of: ").
                    append(lineBreak);

            final ClassLoader parent = classLoader.getParent();
            builder.append(buildClassLoaderHierarchyInfo(parent, lineBreak, tabText, indent + 1));
        }
        return builder.toString();
    }

    private static String buildClassLoaderInfo(ClassLoader classLoader, String urlIndent) {
        if (classLoader instanceof URLClassLoader) {
            final URLClassLoader urlClassLoader = (URLClassLoader) classLoader;
            final String baseInformation = classLoader.toString() + ", URLs=[";

            final StringBuilder builder = new StringBuilder();
            for (URL url : urlClassLoader.getURLs()) {
                if (builder.length() > 0) {
                    builder.append(",");
                }
                builder.append("\n").append(urlIndent).append(url);
            }
            builder.append("]");

            return baseInformation + builder.toString();
        } else {
            return classLoader.toString();
        }
    }

    public static void main(String args[]) throws ClassNotFoundException {
        final Class clazz = Class.forName(args[0]);
        System.out.println(showClassLoaderHierarchy(clazz, false));
        System.out.println(showClassSecurity(clazz));
    }
}
