/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

package com.cloudiussystems.bench;

public class Bench {
	
	static long measure(Benchmark b, int iterations) throws Exception {
		long t1 = System.nanoTime();
		b.run(iterations);
		long t2 = System.nanoTime();
		return t2 - t1;
	}
	
	static void test(Benchmark b) throws Exception {
		int iterations = 10000;
		// warm things up
		b.run(iterations);
		while (measure(b, iterations) < 1000000000) {
			iterations *= 2;
		}
		long t = measure(b, iterations);
		System.out.printf("%8d %s\n", t / iterations, b.getName());
	}
	
	public static void main(String[] args) throws Exception {
		test(new NanoTimeBench());
		test(new ContextSwitchBench());
		// large test requiring more memory:
		//SieveBench small = new SieveBench(10000);
		//System.out.println("Small SieveBench (warmup): "+measure(small, 10000)/1e9);
		//SieveBench sb = new SieveBench(200*1000000);
		//System.out.println("Large SieveBench: "+measure(sb, 1)/1e9);
		test(new NullPointerExceptionBench());
	}
}
