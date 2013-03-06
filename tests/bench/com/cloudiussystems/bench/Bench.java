package com.cloudiussystems.bench;

public class Bench {
	
	static long measure(Benchmark b, int iterations) throws Exception {
		long t1 = System.nanoTime();
		b.run(iterations);
		long t2 = System.nanoTime();
		return t2 - t1;
	}
	
	static void test(Benchmark b) throws Exception {
		int iterations = 1; //10000;
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
		test(new SieveBench(100000));
		// Note: To run a larger SieveBench test, do not remove the above
		// short test - it is necessary to get JIT working (otherwise,
		// since a large test will only have one iteration, the test will
		// not be compiled. Also enabling the following test requires more
		// memory:
		// test(new SieveBench(200*1000000));
		test(new NullPointerExceptionBench());
	}
}