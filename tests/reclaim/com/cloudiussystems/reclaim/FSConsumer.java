/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

package com.cloudiussystems.reclaim;

import java.io.File;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.util.ArrayList;
import java.nio.ByteBuffer;
import java.nio.channels.FileChannel;

public class FSConsumer {

    private JVMConsumer _Jvm;
    private ArrayList<ByteBuffer> BList;
    private final int page = 2 << 20;

    private RandomAccessFile file;

    public FSConsumer() {
        try {
            file = new RandomAccessFile("/tests/reclaim/file", "rw");
        } catch (IOException x) {
            System.err.format("Error writing to file: %s%n", x);
        }
    }

    public void consume_buffers() {

        System.out.println("Consuming " + BList.size() + " buffers...");
        System.out.println("Starting file");
        int idx = 0;

        FileChannel channel;
        try {
            file.setLength(0);
            channel = file.getChannel();
            channel.position(0);
        } catch (IOException x) {
            System.err.format("Error setting up channel for file: %s%n", x);
            return;
        }

        while (!BList.isEmpty()) {
            if ((BList.size() % 10) == 0) {
                System.out.println("Current size: " + BList.size());
            }

            ByteBuffer b = BList.remove(0);

            try {
                while (b.hasRemaining()) {
                    channel.write(b);
                }
            } catch (IOException x) {
                System.err.format("Error writing to file: %s%n", x);
                return;
            }
        }

        System.out.println("Finished write. Reading back...");

        try {
            long size = file.length();
            channel.position(0);
            // Make sure the ARC is stressed. Not all writes will.
            while (channel.position() < channel.size()) {
                ByteBuffer buf = ByteBuffer.allocate(4 << 10);
                channel.read(buf);
            }
        } catch (IOException x) {
            System.err.format("Error closing file: %s%n", x);
            return;
        }

        System.out.println("FILE Done");
        return;
	}

    public void consume() {
        System.out.println("Consumes the buffers");
        consume_buffers();
    }

    public void set_jvm(JVMConsumer jvm) {
        _Jvm = jvm;
        BList = _Jvm.BList;
    }
}
