package tests;

import io.osv.ContextIsolator;

import java.util.concurrent.BrokenBarrierException;
import java.util.concurrent.CyclicBarrier;

import static junit.framework.Assert.assertEquals;

/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
public class StaticFieldSetter {
    public static final String NEW_VALUE = "newValue";
    public static final String OLD_VALUE = "oldValue";

    @SuppressWarnings("FieldCanBeLocal")
    public static String staticField = OLD_VALUE;

    public static void main(String[] args) {
        staticField = NEW_VALUE;
    }

    public static class Party {
        public static void main(String[] args) throws InterruptedException, BrokenBarrierException {
            CyclicBarrier barrier = (CyclicBarrier) ContextIsolator.getInstance().receive();

            String value = args[0];
            staticField = value;

            barrier.await();

            assertEquals(value, staticField);
        }
    }
}
