package io.osv;

import io.osv.util.ClassDiagnostics;

/*
 * Copyright (C) 2016 Waldemar Kozaczuk
 * Copyright (C) 2013-2016 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
public class RunJvmAppHelper {

    public interface JvmFactory {
        Jvm getJvm();
    }

    static public void runSync(JvmFactory jvmFactory, String[] args) {

        if (args.length > 0 && args[0].equals("-version")) {
            System.err.println("java version \"" +
                    System.getProperty("java.version") + "\"");
            System.err.println(System.getProperty("java.runtime.name") +
                    " (" + System.getProperty("java.runtime.version") +
                    ")");
            System.err.println(System.getProperty("java.vm.name") +
                    " (build " + System.getProperty("java.vm.version") +
                    ", " + System.getProperty("java.vm.info") + ")");
            return;
        }

        if(ClassDiagnostics.showDiagnostics(args)) {
            System.out.println("Arguments:");
            for(String arg:args) {
                System.out.println("\t[" + arg + "]");
            }
        }

        try {
            jvmFactory.getJvm().runSync(args);
        } catch (IllegalArgumentException ex) {
            System.err.println("RunJava: " + ex.getMessage());
        } catch (AppThreadTerminatedWithUncaughtException ex) {
            if (ex.getCause() instanceof MainClassNotFoundException) {
                System.err.println("Error: Could not find or load main class " + ((MainClassNotFoundException) ex.getCause()).getClassName());
            } else {
                ex.printStackTrace();
            }
        } catch (Throwable ex) {
            ex.printStackTrace();
        }
    }
}
