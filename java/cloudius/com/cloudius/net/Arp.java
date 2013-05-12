package com.cloudius.net;

import com.cloudius.Config;

public class Arp {
    static {
        Config.loadJNI("networking.so");
    }
    
    public native static void add(String ifname, String macaddr, String ip);
}
