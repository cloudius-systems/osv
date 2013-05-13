package com.cloudius.net;

import com.cloudius.Config;

public class Route {
    static {
        Config.loadJNI("networking.so");
    }

    public native static void add_default(String gw);
}
