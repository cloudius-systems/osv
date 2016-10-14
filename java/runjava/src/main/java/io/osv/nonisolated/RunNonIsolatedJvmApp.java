package io.osv.nonisolated;

/*
 * Copyright (C) 2016 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

import io.osv.Jvm;

import static io.osv.RunJvmAppHelper.runSync;
import static io.osv.RunJvmAppHelper.JvmFactory;

public class RunNonIsolatedJvmApp {
    private static native void onVMStop();

    static {
        Runtime.getRuntime().addShutdownHook(new Thread() {
            public void run() {
                onVMStop();
            }
        });
    }

    public static void main(String[] args) {
        runSync(new JvmFactory() {
            public Jvm getJvm() {
                return NonIsolatedJvm.getInstance();
            }
        }, args);
    }
}
