package io.osv;

import java.io.IOException;
import java.net.URL;
import java.net.URLConnection;

import static java.security.AccessController.checkPermission;

/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
public class TestDomainPermissions {
    public static void main(String[] args) throws ClassNotFoundException, IOException {
        checkHasAccessToFilesFromItsJar();
        checkCanCallSystemExit();
    }

    private static void checkCanCallSystemExit() throws IOException {
        try (TemporarySecurityManager ignored = new TemporarySecurityManager()) {
            System.exit(0);
        }
    }

    private static void checkHasAccessToFilesFromItsJar() throws IOException {
        Class<TestDomainPermissions> clazz = TestDomainPermissions.class;
        ClassLoader classLoader = clazz.getClassLoader();
        URL resource = classLoader.getResource(getClassFilePath(clazz));
        if (resource == null) {
            throw new AssertionError();
        }

        URLConnection urlConnection = resource.openConnection();

        try (TemporarySecurityManager ignored = new TemporarySecurityManager()) {
            checkPermission(urlConnection.getPermission());
        }
    }

    private static String getClassFilePath(Class<?> clazz) {
        return clazz.getName().replace('.', '/') + ".class";
    }
}
