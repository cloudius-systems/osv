package com.cloudius.trace;

import java.io.Closeable;
import java.io.IOException;

public class Counter implements Closeable {

	private long handle;
	
	public Counter(Tracepoint tp) {
		handle = tp.createCounter();
	}

	public long read() {
		return Tracepoint.readCounter(handle);
	}
	
	@Override
	public void finalize() {
		try {
			close();
		} catch (IOException e) {
			// nothing we can do.
		}
	}

	@Override
	public void close() throws IOException {
		if (handle != 0) {
			Tracepoint.destroyCounter(handle);
			handle = 0;
		}
	}
}
