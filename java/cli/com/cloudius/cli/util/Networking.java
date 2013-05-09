package com.cloudius.cli.util;

import java.io.IOException;

import com.cloudius.net.IFConfig;

import sun.org.mozilla.javascript.ScriptableObject;
import sun.org.mozilla.javascript.annotations.JSFunction;

public class Networking extends ScriptableObject {

    // Identifies the scriptable object
    private static final long serialVersionUID = 436644325540039L;

    @JSFunction
    public static boolean set_ip(String ifname, String ip, String netmask)
    {
        try {
            IFConfig.set_ip(ifname, ip, netmask);
            return true;
        } catch (IOException e) {
            return false;
        }
    }
    
    @Override
    public String getClassName() {
        return "Networking";
    }

}
