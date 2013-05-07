package com.cloudius.util;

import com.cloudius.Config;

public class Exec {

    static {
        Config.loadJNI("elf-loader.so");
    }

    public native static boolean run(String[] argv);

    // The native function run() alters this member variable
    public static int _exitcode;

}
