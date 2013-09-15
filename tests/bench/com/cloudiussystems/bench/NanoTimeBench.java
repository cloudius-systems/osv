/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

package com.cloudiussystems.bench;

class NanoTimeBench implements Benchmark {
	
	static long total;
	
	public String getName() {
		return "NanoTime";
	}
	
	public void run(int iterations) {
		for (int i = 0; i < iterations; ++i) {
			total += System.nanoTime();
		}
	}
}
