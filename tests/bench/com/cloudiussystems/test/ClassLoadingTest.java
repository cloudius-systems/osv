package com.cloudiussystems.test;

import java.io.IOException;
import java.io.InputStream;
import java.net.URL;
import java.util.Enumeration;
import java.util.Scanner;

/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

public class ClassLoadingTest {
    public static final String RESOURCE_NAME = "test.txt";
    public static final String RESOURCE_CONTENT = "abc";

    public static void main(String[] args) throws IOException {
        testGetResource();
        testGetResourceAsStream();
        testGetResources();

        System.out.println("OK");
    }

    private static void testGetResources() throws IOException {
        Enumeration<URL> resources = classLoader().getResources(RESOURCE_NAME);
        assertTrue(resources.hasMoreElements());
        assertEquals(readAndClose(resources.nextElement().openStream()), RESOURCE_CONTENT);
        assertFalse(resources.hasMoreElements());
    }

    private static void assertTrue(boolean value) {
        assertEquals(value, true);
    }

    private static void assertFalse(boolean value) {
        assertEquals(value, false);
    }

    private static void testGetResource() throws IOException {
        URL resource = classLoader().getResource(RESOURCE_NAME);
        assertNotNull(resource);
        assertEquals(readAndClose(resource.openStream()), RESOURCE_CONTENT);
    }

    private static ClassLoader classLoader() {
        return ClassLoader.getSystemClassLoader();
    }

    private static void testGetResourceAsStream() throws IOException {
        InputStream stream = classLoader().getResourceAsStream(RESOURCE_NAME);
        assertNotNull(stream);
        assertEquals(readAndClose(stream), RESOURCE_CONTENT);
    }

    private static String readAndClose(InputStream inputStream) throws IOException {
        try {
            return read(inputStream);
        } finally {
            inputStream.close();
        }
    }

    private static void assertNotNull(Object value) {
        if (value == null) {
            throw new AssertionError("Expected non null");
        }
    }

    private static void assertEquals(Object actual, Object expected) {
        if (!actual.equals(expected)) {
            throw new AssertionError("Expected " + expected + "\nbut got: " + actual);
        }
    }

    private static String read(InputStream inputStream) {
        Scanner scanner = new Scanner(inputStream).useDelimiter("\\A");
        if (!scanner.hasNext()) {
            return null;
        }
        return scanner.next();
    }
}
