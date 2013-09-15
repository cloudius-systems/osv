/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

package com.cloudius.sshd;

import com.cloudius.util.IStty;

public class Stty implements IStty {
    @Override
    public void reset() {
    }

    @Override
    public void close() throws Exception {
    }

    @Override
    public void raw() {
    }
}
