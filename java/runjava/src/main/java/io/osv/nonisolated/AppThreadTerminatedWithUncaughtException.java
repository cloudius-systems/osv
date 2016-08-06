package io.osv.nonisolated;

/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
public class AppThreadTerminatedWithUncaughtException extends Exception {
    public AppThreadTerminatedWithUncaughtException(Throwable cause) {
        super(cause);
    }
}
