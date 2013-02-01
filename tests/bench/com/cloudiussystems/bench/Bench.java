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
	}
}