package tests;

import java.util.logging.FileHandler;
import java.util.logging.Level;
import java.util.logging.Logger;
import java.util.logging.SimpleFormatter;

/*
 * Copyright (C) 2016 Waldemar Kozaczuk
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
public class NonIsolatedLoggingProcess {
    public static void main(String[] args) throws Throwable {
        String logFileName = args[0];
        String loggerName = args[1];
        Level level = Level.parse(args[2]);
        String message = args[3];

        FileHandler handler = new FileHandler(logFileName);
        handler.setFormatter(new SimpleFormatter());
        Logger logger = Logger.getLogger(loggerName);
        logger.addHandler(handler);

        logger.setLevel(level);

        logger.info(message);
        logger.warning(message);
    }
}
