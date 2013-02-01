package com.cloudiussystems.bench;

public class ContextSwitchBench implements Benchmark {

	private int remaining;
	private Exception fail;
	
	synchronized void complete() {
		notify();
	}
	
	synchronized void report(Exception ex) {
		fail = ex;
		notify();
	}
	
	private class TestThread extends Thread {
		
		public TestThread other;
		boolean sleeping = true;
	
		public TestThread() {
			this.setDaemon(true);
		}
		
		public synchronized void wake() {
			sleeping = false;
			notify();
		}

		private synchronized void loop(TestThread tt, TestThread other)
				throws InterruptedException {
			while (true) {
				while (sleeping) {
					wait();
				}
				if (remaining > 0) {
					--remaining;
					other.wake();
				} else {
					complete();
				}
				sleeping = true;
			}
		}

		public void run() {
			while (true) {
				try {
					loop(this, other);
				} catch (Exception ex) {
					report(ex);
				}
			}
		}
	}
	
	private TestThread t1 = new TestThread();
	private TestThread t2 = new TestThread();
	
	public ContextSwitchBench() {
		t1.other = t2;
		t2.other = t1;
		t1.start();
		t2.start();
	}
	
	@Override
	public String getName() {
		return "context switch";
	}

	@Override
	public synchronized void run(int iterations) throws Exception {
		remaining = iterations;
		t1.wake();
		while (remaining > 0 && fail == null) {
			wait();
		}
		if (fail != null) {
			throw new Exception("Thread failed", fail);
		}
	}
}
