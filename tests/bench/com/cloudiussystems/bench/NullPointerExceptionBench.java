/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

package com.cloudiussystems.bench;

public class NullPointerExceptionBench implements Benchmark {

	public static int x;
	public static Object o; // try to defeat optimization
	
	@Override
	public String getName() {
		return "NullPointerException";
	}

	@Override
	public void run(int iterations) throws Exception {
		for (int i = 0; i < iterations; ++i) {
			x += testException(o);
		}
	}
	
	private int testException(Object o) {
		try {
			return o.hashCode();
		} catch (NullPointerException npe) {
			return 0;
		}
	}

}
