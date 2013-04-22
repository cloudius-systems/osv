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
