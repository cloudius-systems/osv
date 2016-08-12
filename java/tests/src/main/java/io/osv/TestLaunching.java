package io.osv;

import java.io.File;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

import io.osv.isolated.Context;
import io.osv.isolated.IsolatedJvm;
import io.osv.nonisolated.NonIsolatedJvm;

import static java.util.Arrays.asList;
import static org.fest.assertions.Assertions.assertThat;

/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
public class TestLaunching {

    public static Context runIsolate(Class<?> clazz, String... programArgs) throws Throwable {
        return runIsolate(clazz, Collections.<String>emptyList(), programArgs);
    }

    public static Context runIsolate(Class<?> clazz, List<String> args, String... programArgs) throws Throwable {
        List<String> allArgs = testJarAndPrepareArgs(clazz, args, programArgs);
        return IsolatedJvm.getInstance().run(allArgs.toArray(new String[allArgs.size()]));
    }

    public static Thread runWithoutIsolation(Class<?> clazz, String... programArgs) throws Throwable {
        return runWithoutIsolation(clazz, Collections.<String>emptyList(), programArgs);
    }

    public static Thread runWithoutIsolation(Class<?> clazz, List<String> args, String... programArgs) throws Throwable {
        List<String> allArgs = testJarAndPrepareArgs(clazz, args, programArgs);
        return NonIsolatedJvm.getInstance().run(allArgs.toArray(new String[allArgs.size()]));
    }

    private static List<String> testJarAndPrepareArgs(Class<?> clazz, List<String> args, String... programArgs) {
        String jarPath = System.getProperty("isolates.jar");
        assertThat(jarPath).isNotEmpty();
        assertThat(new File(jarPath)).exists();

        ArrayList<String> allArgs = new ArrayList<>();
        allArgs.addAll(args);
        allArgs.add("-cp");
        allArgs.add(jarPath);
        allArgs.add(clazz.getName());
        allArgs.addAll(asList(programArgs));

        return allArgs;
    }
}
