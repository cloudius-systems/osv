package com.cloudius.cli.util;

import java.io.IOException;

import com.cloudius.net.Arp;
import com.cloudius.net.DHCP;
import com.cloudius.net.IFConfig;
import com.cloudius.net.Route;

public class Networking  {

    public static boolean set_ip(String ifname, String ip, String netmask)
    {
        try {
            IFConfig.set_ip(ifname, ip, netmask);
            return true;
        } catch (IOException e) {
            return false;
        }
    }
    
    public static boolean if_up(String ifname)
    {
        try {
            IFConfig.if_up(ifname);
            return true;
        } catch (IOException e) {
            return false;
        }
    }
    
    public static void arp_add(String ifname, String macaddr, String ip)
    {
        Arp.add(ifname, macaddr, ip);
    }
    
    public static void route_add_default(String gw)
    {
        Route.add_default(gw);
    }
    
    public static void dhcp_start()
    {
        DHCP.dhcp_start();
    }
    
}
