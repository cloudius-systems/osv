package io.osv;

import java.io.File;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

import static java.util.Arrays.asList;
import static org.fest.assertions.Assertions.assertThat;

/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
public class TestIsolateLaunching {

    public static Context runIsolate(Class<?> clazz, String... programArgs) throws Throwable {
        return runIsolate(clazz, Collections.<String>emptyList(), programArgs);
    }

    public static Context runIsolate(Class<?> clazz, List<String> args, String... programArgs) throws Throwable {
        String jarPath = System.getProperty("isolates.jar");
        assertThat(jarPath).isNotEmpty();
        assertThat(new File(jarPath)).exists();

        ArrayList<String> allArgs = new ArrayList<>();
        allArgs.addAll(args);
        allArgs.add("-cp");
        allArgs.add(jarPath);
        allArgs.add(clazz.getName());
        allArgs.addAll(asList(programArgs));

        return ContextIsolator.getInstance().run(allArgs.toArray(new String[allArgs.size()]));
    }

}
