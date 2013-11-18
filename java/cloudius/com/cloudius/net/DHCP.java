/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

package com.cloudius.net;

import com.cloudius.Config;

public class DHCP {
    static {
        Config.loadJNI("networking.so");
    }

    public native static void dhcp_start();
}
