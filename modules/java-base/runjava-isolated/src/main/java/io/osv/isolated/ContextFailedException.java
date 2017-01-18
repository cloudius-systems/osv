package io.osv.isolated;

import io.osv.AppThreadTerminatedWithUncaughtException;

/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
public class ContextFailedException extends AppThreadTerminatedWithUncaughtException {
    public ContextFailedException(Throwable cause) {
        super(cause);
    }
}
