package io.osv;

import org.junit.Test;
import tests.LoggingProcess;

import java.io.File;
import java.io.IOException;
import java.util.concurrent.CyclicBarrier;

import static io.osv.TestIsolateLaunching.runIsolate;
import static org.apache.commons.io.FileUtils.forceDeleteOnExit;
import static org.apache.commons.io.FileUtils.readLines;
import static org.fest.assertions.Assertions.assertThat;

/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

public class LoggingIsolationTest {
    private static final String LOGGER_NAME = "test-logger";

    @Test
    public void testLoggerLevelChangesAreIsolated() throws Throwable {
        File log1 = newTemporaryFile();
        File log2 = newTemporaryFile();

        Context ctx1 = runIsolate(LoggingProcess.class, log1.getAbsolutePath(), LOGGER_NAME, "INFO", "ctx1");
        Context ctx2 = runIsolate(LoggingProcess.class, log2.getAbsolutePath(), LOGGER_NAME, "WARNING", "ctx2");

        CyclicBarrier barrier = new CyclicBarrier(2);
        ctx1.send(barrier);
        ctx2.send(barrier);

        ctx1.join();
        ctx2.join();

        assertThat(readLines(log1))
                .hasSize(4)
                .contains("INFO: ctx1")
                .contains("WARNING: ctx1");

        assertThat(readLines(log2))
                .hasSize(2)
                .contains("WARNING: ctx2")
                .excludes("INFO: ctx2");
    }

    private File newTemporaryFile() throws IOException {
        File file = File.createTempFile("test", null);
        forceDeleteOnExit(file);
        return file;
    }
}
