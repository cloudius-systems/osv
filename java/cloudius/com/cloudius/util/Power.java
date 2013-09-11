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
