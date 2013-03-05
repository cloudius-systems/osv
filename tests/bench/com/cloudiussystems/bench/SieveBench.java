package com.cloudiussystems.bench;

// Sieve of Eratosthenes - a memory thrashing benchmark

class SieveBench implements Benchmark {
	boolean[] composite;
	
	public SieveBench(int memsize){
		composite = new boolean[memsize]; // initialized to false
	}
	
	public String getName() {
		return "Sieve-"+composite.length;
	}
	
	public void run(int iterations) {
		for (int i = 0; i < iterations; ++i) {
			for(int n=2; n<composite.length; n++){
				if(composite[n])
					continue;
				//System.out.println(n+" is prime");
				for(int k=n*2; k<composite.length; k+=n){
					composite[k]=true;
				}
			}
		}
	}
}
