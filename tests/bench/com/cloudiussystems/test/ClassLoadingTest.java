package com.cloudiussystems.test;

import java.io.IOException;
import java.io.InputStream;
import java.net.URL;
import java.net.URLClassLoader;
import java.util.Enumeration;
import java.util.Scanner;

/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

public class ClassLoadingTest {
    private static final String RESOURCE_NAME = "test.txt";
    private static final String RESOURCE_CONTENT = "abc";
    private static final ClassLoader systemLoader = ClassLoader.getSystemClassLoader();

    public static void main(String[] args) throws IOException {
        testGetResource();
        testGetResourceAsStream();
        testGetResources();
        testGettingResourceFromChild();
        testAssertionStatusManipulation();

        System.out.println("OK");
    }

    private static void testAssertionStatusManipulation() {
        testAssertionStatusCanBeSet();
        testAssertionStatusCanBeCleared();
        testPackageAssertionStatusCanBeSet();
        testDefaultAssertionStatusCanBeSet();
    }

    private static void testAssertionStatusCanBeSet() {
        systemLoader.clearAssertionStatus();
        systemLoader.setClassAssertionStatus(ClassWithAssertion.class.getName(), true);
        assertTrue(ClassWithAssertion.assertExecuted);
    }

    private static void testAssertionStatusCanBeCleared() {
        systemLoader.clearAssertionStatus();
        systemLoader.setClassAssertionStatus(ClassWithAssertion2.class.getName(), true);
        systemLoader.clearAssertionStatus();
        assertFalse(ClassWithAssertion2.assertExecuted);
    }

    private static void testPackageAssertionStatusCanBeSet() {
        systemLoader.clearAssertionStatus();
        systemLoader.setPackageAssertionStatus(ClassWithAssertion3.class.getPackage().getName(), true);
        assertTrue(ClassWithAssertion3.assertExecuted);
    }

    private static void testDefaultAssertionStatusCanBeSet() {
        systemLoader.clearAssertionStatus();
        systemLoader.setDefaultAssertionStatus(true);
        assertTrue(ClassWithAssertion4.assertExecuted);
    }

    private static void testGettingResourceFromChild() throws IOException {
        URLClassLoader child = new URLClassLoader(new URL[]{}, systemLoader);

        assertNotNull(child.getResource(RESOURCE_NAME));
        assertTrue(child.getResources(RESOURCE_NAME).hasMoreElements());
    }

    private static void testGetResources() throws IOException {
        Enumeration<URL> resources = systemLoader.getResources(RESOURCE_NAME);
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
        URL resource = systemLoader.getResource(RESOURCE_NAME);
        assertNotNull(resource);

        //noinspection ConstantConditions
        assertEquals(readAndClose(resource.openStream()), RESOURCE_CONTENT);
    }

    private static void testGetResourceAsStream() throws IOException {
        InputStream stream = systemLoader.getResourceAsStream(RESOURCE_NAME);
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
