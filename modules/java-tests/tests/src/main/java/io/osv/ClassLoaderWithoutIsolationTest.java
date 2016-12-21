package io.osv;

import io.osv.nonisolated.NonIsolatedJvm;
import org.junit.Test;
import tests.*;

import static io.osv.TestLaunching.runWithoutIsolation;
import static org.junit.Assert.*;

/*
 * Copyright (C) 2016 Waldemar Kozaczuk
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
public class ClassLoaderWithoutIsolationTest {
    private static final Class<?> THIS_CLASS = ClassLoaderWithoutIsolationTest.class;

    @Test
    public void testParentContextSeesModificationsOfStaticFields() throws Throwable {
        Thread thread = runWithoutIsolation(StaticFieldSetter.class); // This is where the class is loaded and available to child one
        thread.join();

        //
        // Rethrow any exception that may have been raised and led to the thread terminating
        final Throwable exception = NonIsolatedJvm.getInstance().getThrownExceptionIfAny();
        if (null != exception)
            throw exception;
        //
        // There is one class instance of StaticFieldSetter loaded as there is no isolation
        // between the parent app classloader (at the tests runner level) and the child app
        // classloader level which is created when runWithoutIsolation is called
        assertEquals(StaticFieldSetter.NEW_VALUE, StaticFieldSetter.staticField);
    }

    @Test
    public void testChildSeesSameVersionOfAClassDefinedInParentContext() throws Throwable {
        String fieldName = "field_existing_only_in_isolate_context";

        try {
            ClassPresentInBothContexts.class.getDeclaredField(fieldName);
            throw new AssertionError("The field should be absent in parent context");
        } catch (NoSuchFieldException e) {
            // expected
        }

        Thread thread = runWithoutIsolation(FieldTester.class, ClassPresentInBothContexts.class.getName(), fieldName);
        thread.join();
        //
        // Rethrow any exception that may have been raised and led to the thread terminating
        final Throwable exception = NonIsolatedJvm.getInstance().getThrownExceptionIfAny();
        if (null != exception && exception instanceof NoSuchFieldException) {
            // It is what is expected as there is no isolation between child and parent classloader the class loaded
            // by parent classloader from tests.jar which is a first jar in the classpath
        } else if (null != exception) {
            throw exception;
        } else {
            throw new AssertionError("The field should be also absent in child context");
        }
    }

    @Test
    public void testClassesDefinedInParentContextAreVisibleToChild() throws Throwable {
        Thread thread = runWithoutIsolation(ClassFinder.class, ClassPresentOnlyInParentContext.class.getName());
        thread.join();
        //
        // Rethrow any exception that may have been raised and led to the thread terminating
        final Throwable exception = NonIsolatedJvm.getInstance().getThrownExceptionIfAny();
        if (null != exception)
            throw exception;
        //
        // As there is no isolation between child and parent classloader the class loaded
        // by parent classloader ClassPresentOnlyInParentContext from tests.jar will
        // actually be visible in the children
    }

    @Test
    public void testClassesFromExtensionDirectoryCanBeLoaded() throws Exception {
        assertNotNull(SomeExtensionClass.class);
    }

    @Test
    public void testClassPutInRootDirectoryIsNotPickedUpByDefaultSystemClassLoader() throws Exception {
        assertSame(ClassPutInRoot.class.getClassLoader(), THIS_CLASS.getClassLoader());
    }
}