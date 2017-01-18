/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

package com.cloudius.util;

import java.net.URL;
import java.net.URLClassLoader;

public class PrintClassPath {

    public static void main (String args[]) {

        ClassLoader cl = ClassLoader.getSystemClassLoader();

        URL[] urls = ((URLClassLoader)cl).getURLs();

        for(URL url : urls) {
            System.out.println(url.getFile());
        }

    }
}
