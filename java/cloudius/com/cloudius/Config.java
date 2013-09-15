/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

package com.cloudius;


public class Config {
	/**
	 * loadJNI loads a shared object for JNI from OSV's default
	 * directory for these files (/usr/lib/jni). 
	 * @param soname
	 */
	public static void loadJNI(String soname) {
		System.load("/usr/lib/jni/"+soname);
	}
}
