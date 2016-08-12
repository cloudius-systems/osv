package io.osv;

import io.osv.nonisolated.NonIsolatedJvm;
import org.junit.Test;
import tests.NonIsolatedLoggingProcess;

import java.io.File;
import java.io.IOException;
import java.util.List;

import static io.osv.TestLaunching.runWithoutIsolation;
import static org.apache.commons.io.FileUtils.forceDeleteOnExit;
import static org.apache.commons.io.FileUtils.readLines;
import static org.fest.assertions.Assertions.assertThat;

/*
 * Copyright (C) 2016 Waldemar Kozaczuk
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
public class LoggingWithoutIsolationTest {
    private static final String LOGGER_NAME = "test-logger";

    @Test
    public void testLogger() throws Throwable {
        File log = newTemporaryFile();

        Thread thread = runWithoutIsolation(NonIsolatedLoggingProcess.class, log.getAbsolutePath(), LOGGER_NAME, "INFO", "ctx");
        thread.join();

        //
        // Rethrow any exception that may have been raised and led to the thread terminating
        final Throwable exception = NonIsolatedJvm.getInstance().getThrownExceptionIfAny();
        if( null != exception)
            throw exception;

        final List<String> logLines = readLines(log);
        for( String line : logLines)
            System.out.println(line);

        assertThat(logLines)
                .hasSize(4)
                .contains("INFO: ctx")
                .contains("WARNING: ctx");
    }

    private File newTemporaryFile() throws IOException {
        File file = File.createTempFile("test", null);
        forceDeleteOnExit(file);
        return file;
    }
}
