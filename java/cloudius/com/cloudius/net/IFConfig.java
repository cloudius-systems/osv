package com.cloudius.net;

import java.io.IOException;

import com.cloudius.Config;

public class IFConfig {
    static {
        Config.loadJNI("networking.so");
    }
    
    public native static void set_ip(String ifname, String ip, String netmask)
            throws IOException;
    
    public native static void if_up(String ifname) throws IOException;
    
}
