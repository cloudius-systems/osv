package io.osv;

import org.junit.Test;

import java.io.File;
import java.io.IOException;
import java.util.List;

import java.util.logging.FileHandler;
import java.util.logging.SimpleFormatter;
import java.util.logging.Logger;
import static java.util.logging.Level.INFO;

import static org.apache.commons.io.FileUtils.forceDeleteOnExit;
import static org.apache.commons.io.FileUtils.readLines;
import static org.fest.assertions.Assertions.assertThat;

/*
 * Copyright (C) 2021 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
public class LoggingTest {
    private static final String LOGGER_NAME = "test-logger";

    @Test
    public void testLogger() throws Throwable {
        File log = newTemporaryFile();

	Thread thread = new Thread(() -> {
            try {
                FileHandler handler = new FileHandler(log.getAbsolutePath());
                handler.setFormatter(new SimpleFormatter());

                Logger logger = Logger.getLogger(LOGGER_NAME);
                logger.addHandler(handler);
                logger.setLevel(INFO);

                logger.info("ctx");
                logger.warning("ctx");
            } catch (IOException ex) {
                System.err.println("Error: " + ex.toString());
                assertThat(true).isFalse();
            }
        });
        thread.start();
        thread.join();

        final List<String> logLines = readLines(log);
        for (String line : logLines)
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
