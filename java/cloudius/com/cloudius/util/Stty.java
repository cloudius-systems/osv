package com.cloudius.util;

import com.cloudius.Config;

public class Stty {
	static {
	    Config.loadJNI("stty.so");
	}
	
	private long savedStateAddr;
	
	private native long saveState();
	private native void freeState(long addr);

	public Stty() {
		savedStateAddr = saveState();
	}
	
	protected void finalize() throws Throwable {
		close();
	}
	
	public native void raw();
	private native void reset(long addr);
	public void reset() {
		reset(savedStateAddr);
	}
	public void close() throws Exception {
		reset(savedStateAddr);
		freeState(savedStateAddr);
		savedStateAddr = 0;
	}
	
}
