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
