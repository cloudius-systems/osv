/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

package com.cloudiussystems.bench;

// Sieve of Eratosthenes - a memory thrashing benchmark

class SieveBench implements Benchmark {
	boolean[] composite;
	
	public SieveBench(int memsize){
		composite = new boolean[memsize];
	}
	
	public String getName() {
		return "Sieve-"+composite.length;
	}
	
	void doskip(int n){
		for(int k=n*2; k<composite.length; k+=n){
			composite[k]=true;
		}
	}
	
	public void run(int iterations) {
		for (int i = 0; i < iterations; ++i) {
			for(int n=2; n<composite.length; n++){
				composite[n] = false;
			}
			for(int n=2; n<composite.length; n++){
				if(composite[n])
					continue;
				//System.out.println(n+" is prime");
				doskip(n);
			}
		}
	}
	
}
