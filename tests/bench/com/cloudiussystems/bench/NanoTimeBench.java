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
