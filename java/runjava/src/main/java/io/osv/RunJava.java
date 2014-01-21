package io.osv;

/*
 * Copyright (C) 2013-2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

public class RunJava {

    public static void main(String[] args) {
        try {
            ContextIsolator.getInstance().runSync(args);
        } catch (Throwable ex) {
            ex.printStackTrace();
        }
    }
}
