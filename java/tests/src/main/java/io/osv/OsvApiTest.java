package io.osv;

import org.junit.Test;

import static org.fest.assertions.Assertions.assertThat;

/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
public class OsvApiTest {
    @Test
    public void testOSvVersionIsSet() {
        assertThat(System.getProperty("osv.version"))
                .isNotEmpty()
                .matches("v\\d+\\.\\d+([-a-z0-9]+)?");
    }
}
