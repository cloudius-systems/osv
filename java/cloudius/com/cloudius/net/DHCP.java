package com.cloudius.net;

import com.cloudius.Config;

public class DHCP {
    static {
        Config.loadJNI("networking.so");
    }
    
    public native static void dhcp_start();
}
