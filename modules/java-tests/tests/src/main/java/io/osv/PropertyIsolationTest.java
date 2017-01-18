package io.osv;

import org.junit.Test;
import tests.PropertyReader;
import tests.PropertySetter;

import java.util.concurrent.CyclicBarrier;

import io.osv.isolated.Context;

import static io.osv.TestLaunching.runIsolate;
import static java.util.Arrays.asList;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNotNull;

/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
public class PropertyIsolationTest {
    public static final String PROPERTY_NAME = "test.property";
    public static final String VALUE_1 = "value1";
    public static final String VALUE_2 = "value2";
    public static final String LINE_SEPARATOR = "line.separator";

    private final CyclicBarrier barrier = new CyclicBarrier(2);

    public static String define(String key, String value) {
        return "-D" + key + "=" + value;
    }

    @Test
    public void testPropertiesPassedAsArgumentsAreNotShared() throws Throwable {
        Context context1 = runIsolate(PropertyReader.class, asList(define(PROPERTY_NAME, VALUE_1)), PROPERTY_NAME, VALUE_1);
        Context context2 = runIsolate(PropertyReader.class, asList(define(PROPERTY_NAME, VALUE_2)), PROPERTY_NAME, VALUE_2);

        context1.send(barrier);
        context2.send(barrier);

        context1.join();
        context2.join();
    }

    @Test
    public void testPropertySetInEachContextWillNotOverrideValueInOtherContexts() throws Throwable {
        Context context1 = runIsolate(PropertySetter.class, PROPERTY_NAME, VALUE_1);
        Context context2 = runIsolate(PropertySetter.class, PROPERTY_NAME, VALUE_2);

        context1.send(barrier);
        context2.send(barrier);

        context1.join();
        context2.join();
    }

    @Test
    public void testPropertiesAreNotInheritedAndChangesInParentContextDoNotAffectValuesInTheChild() throws Throwable {
        System.setProperty(PROPERTY_NAME, VALUE_1);

        Context context = runIsolate(PropertyReader.class, PROPERTY_NAME, null);
        context.send(barrier);

        System.setProperty(PROPERTY_NAME, VALUE_2);
        barrier.await();

        context.join();
    }

    @Test
    public void testInitialSystemPropertiesAreVisibleInChildContexts() throws Throwable {
        String lineSeparator = System.getProperty(LINE_SEPARATOR);
        assertNotNull(lineSeparator);
        assertNotEquals("", lineSeparator);

        Context context = runIsolate(PropertyReader.class, LINE_SEPARATOR, lineSeparator);
        context.send(barrier);

        barrier.await();
        context.join();
    }
}
