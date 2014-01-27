package tests;

import io.osv.ContextIsolator;

import java.util.concurrent.CyclicBarrier;

import static org.junit.Assert.assertEquals;

/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
public class PropertyReader {
    public static void main(String[] args) throws Exception {
        String property = args[0];
        String expectedValue = args[1];

        CyclicBarrier barrier = (CyclicBarrier) ContextIsolator.getInstance().receive();
        barrier.await();

        assertEquals(expectedValue, System.getProperty(property));
    }
}
