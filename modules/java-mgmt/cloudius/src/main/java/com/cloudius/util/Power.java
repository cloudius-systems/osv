/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

package com.cloudius.util;

import com.cloudius.Config;

public class Power {
    static {
        Config.loadJNI("power.so");
    }

    public static native void reboot();

    public static void main (String args[]) {
        System.out.println("Rebooting\n");
        reboot();
    }
}
