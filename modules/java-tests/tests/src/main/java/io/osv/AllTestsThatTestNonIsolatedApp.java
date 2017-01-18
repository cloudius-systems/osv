package io.osv;

/*
 * Copyright (C) 2016 Waldemar Kozaczuk
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

import org.junit.runner.RunWith;
import org.junit.runners.Suite;

@RunWith(Suite.class)
@Suite.SuiteClasses({
        LoggingWithoutIsolationTest.class,
        ClassLoaderWithoutIsolationTest.class,
        OsvApiTest.class
})
public class AllTestsThatTestNonIsolatedApp {
}
