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
public class PropertySetter {
    public static void main(String[] args) throws Exception {
        CyclicBarrier barrier = (CyclicBarrier) ContextIsolator.getInstance().receive();
        String property = args[0];
        String value = args[1];

        System.setProperty(property, value);

        barrier.await();

        assertEquals(value, System.getProperty(property, value));
    }
}
