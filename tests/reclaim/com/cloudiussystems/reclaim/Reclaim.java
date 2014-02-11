/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

package com.cloudiussystems.reclaim;
import java.lang.Runtime;

public class Reclaim {

    static long memory = Runtime.getRuntime().maxMemory();

	public static void main(String[] args) {
        JVMConsumer jvm = new JVMConsumer();
        FSConsumer  fs  = new FSConsumer();

        System.out.println("Starting reclaimer test. Max Heap: " + memory);
        jvm.set_fs(fs);
        fs.set_jvm(jvm);

        for (int i = 1; i < 10; i++) {
            System.out.println("Iteration number " + i);
            jvm.alloc(i * memory / 10);
            fs.consume();
        }
	}
}
