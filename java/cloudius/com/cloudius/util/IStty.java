/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

package com.cloudius.util;

public interface IStty {
    public void raw();
    public void reset();
    public void close() throws Exception;
}
