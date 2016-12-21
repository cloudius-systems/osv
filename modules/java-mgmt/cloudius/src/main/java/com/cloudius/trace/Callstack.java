/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

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
