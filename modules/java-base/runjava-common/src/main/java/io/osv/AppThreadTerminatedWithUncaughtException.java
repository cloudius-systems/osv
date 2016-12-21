package io.osv;

/*
 * Copyright (C) 2016 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
public class AppThreadTerminatedWithUncaughtException extends Exception {
    public AppThreadTerminatedWithUncaughtException(Throwable cause) {
        super(cause);
    }
}
