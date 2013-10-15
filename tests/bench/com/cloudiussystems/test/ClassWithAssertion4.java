package com.cloudiussystems.test;

/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
public class ClassWithAssertion4 {
    static boolean assertExecuted;

    static {
        //noinspection AssertWithSideEffects,ConstantConditions
        assert assertExecuted = true;
    }

}
