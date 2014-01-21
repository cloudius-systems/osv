package io.osv.util;

import java.util.concurrent.Callable;

/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

/**
 * This class provides thread-safe lazy initialization facility with
 * semantics similar to static class initialization.
 *
 * Allows to obtain not-yet-initialized object from the thread which performs the initialization.
 * Creation and initialization may be performed only once.
 *
 * If initialization or construction fails, all subsequent attempts to get that object will fail with that cause.
 *
 */
public final class LazilyInitialized<T> {
    public interface Initializer<T> {
        void initialize(T object) throws Exception;
    }

    private final Callable<T> factory;
    private final Initializer<T> initializer;

    private T initializingValue;
    private volatile T value;
    private volatile Throwable exception;

    public LazilyInitialized(Callable<T> factory, Initializer<T> initializer) {
        this.factory = factory;
        this.initializer = initializer;
    }

    public LazilyInitialized(Callable<T> factory) {
        this.factory = factory;
        this.initializer = null;
    }

    public T get() throws InitializationException {
        if (value != null) {
            return value;
        }

        if (exception != null) {
            throw new InitializationException(exception);
        }

        synchronized (this) {
            if (initializingValue != null) {
                return initializingValue;
            }

            try {
                initializingValue = factory.call();

                if (initializer != null) {
                    initializer.initialize(initializingValue);
                }
            } catch (Throwable e) {
                exception = e;
                throw new InitializationException(e);
            }

            value = initializingValue;
            return value;
        }
    }
}
