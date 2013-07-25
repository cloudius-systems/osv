package com.cloudius.trace;

import com.cloudius.Config;

public class Callstack {

	static {
		Config.loadJNI("tracepoint.so");
	}
	
	public static native Callstack[] collect(Tracepoint tp, int depth, int count, long millis);
	
	private int hits;

	private long[] pc;
	
	private Callstack(int hits, long[] pc) {
		this.hits = hits;
		this.pc = pc;
	}
	
	public int getHits() {
		return hits;
	}
	
	public long[] getProgramCounters() {
		return pc;
	}
}
