/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

package com.cloudiussystems.reclaim;

import java.util.ArrayList;
import java.nio.ByteBuffer;

public class JVMConsumer {

    private FSConsumer _Fs;
    // To keep references alive
    public ArrayList<ByteBuffer> BList = new ArrayList<ByteBuffer>();
    private final int page = 2 << 20;
    static long total_memory = Runtime.getRuntime().maxMemory();

    public static long printMemory(long missing) {
        long free_memory = Runtime.getRuntime().freeMemory();
        if ((missing % 10) == 0) {
            System.out.println("Free Memory: " + free_memory + ". Total " +  total_memory + " ("  + (free_memory * 100.00) / total_memory + " %). " + missing + " more to go");
        }
        return free_memory;
    }

    public void createBuffers(long buffers) {

        for (long i = 0; i < buffers; i++) {
            long free_memory = Runtime.getRuntime().freeMemory();
            ByteBuffer buf  = ByteBuffer.allocate(page);
            buf.putLong(free_memory);
            BList.add(buf);
            printMemory(buffers - i);

        }
        printMemory(0);
	}

    public void alloc(long bytes) {
        createBuffers(bytes / page);
    }

    public void set_fs(FSConsumer fs)
    {
        _Fs = fs;
    }
}
