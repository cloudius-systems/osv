package com.cloudius.balloon;
import java.util.LinkedList;
import java.util.List;

import com.cloudius.Config;


public class Balloon {
	static {
		Config.loadJNI("balloon.so");
	}
	private native boolean giveup(byte[] buffer);
	
	// balloonBuffers is a list of objects allocated on the heap and then
	// given up to the system, using giveup(). We need to keep references
	// to these objects, so they won't be freed by the GC (the whole point
	// of the balloon is that they continue to take up space in the heap).
	// Note that these objects *can* move in the heap (and we need to
	// handle this by finding where they move to).
	private List<byte[]> balloonBuffers = new LinkedList<byte[]>();
	
	public void inflate(int size){
		// TODO: don't allocate the whole size at once, but rather
		// divide it into chunks of some size, say 10MB.
		byte[] buf = new byte[size];
		balloonBuffers.add(buf);
		if(!giveup(buf)) {
			throw new RuntimeException("Balloon.giveup() failed!");
		}
		System.out.println("inflate("+size+") done");
	}
	
	public static void main(String[] args) {
		Balloon b = new Balloon();
		for(int i=18; i<20; i++){
			int size = 1<<i;
			System.out.println("Trying size "+size);
			b.inflate(size);	
			// exercise gc a bit, to cause object movements
			for(int j=0; j<1000; j++){
				System.out.println(j);
				byte[] a = new byte[20000];
			}
			System.gc(); // exercise gc a bit, to cause object movements
		}
		System.out.println("Done.");
	}
}
