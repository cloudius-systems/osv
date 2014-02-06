package io.osv;

import org.junit.Test;
import tests.*;

import java.util.concurrent.CyclicBarrier;

import static io.osv.TestIsolateLaunching.runIsolate;
import static org.junit.Assert.assertEquals;

/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
public class ClassLoaderIsolationTest {

    @Test
    public void testParentContextDoesNotSeeModificationsOfStaticFields() throws Throwable {
        Context context = runIsolate(StaticFieldSetter.class);
        context.join();

        assertEquals(StaticFieldSetter.OLD_VALUE, StaticFieldSetter.staticField);
    }

    @Test
    public void testStaticDataIsIsolatedBetweenChildContexts() throws Throwable {
        Context context1 = runIsolate(StaticFieldSetter.Party.class, "value1");
        Context context2 = runIsolate(StaticFieldSetter.Party.class, "value2");

        CyclicBarrier barrier = new CyclicBarrier(2);
        context1.send(barrier);
        context2.send(barrier);

        context1.join();
        context2.join();
    }

    @Test
    public void testIsolateMayLoadItsOwnVersionOfAClassDefinedInParentContext() throws Throwable {
        String fieldName = "field_existing_only_in_isolate_context";

        try {
            ClassPresentInBothContexts.class.getDeclaredField(fieldName);
            throw new AssertionError("The field should be absent in parent context");
        } catch (NoSuchFieldException e) {
            // expected
        }

        Context context = runIsolate(FieldTester.class, ClassPresentInBothContexts.class.getName(), fieldName);
        context.join();
    }

    @Test
    public void testClassesDefinedInParentContextAreNotVisibleToChild() throws Throwable {
        try {
            Context context = runIsolate(ClassFinder.class, ClassPresentOnlyInParentContext.class.getName());
            context.join();
            throw new AssertionError("Isolate should not be able to find the class");
        } catch (ContextFailedException e) {
            if (e.getCause() instanceof ClassNotFoundException) {
                // expected
            } else {
                throw e;
            }
        }
    }
}
