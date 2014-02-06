package io.osv.jul;

import io.osv.Context;
import io.osv.ContextIsolator;

import java.beans.PropertyChangeListener;
import java.io.IOException;
import java.io.InputStream;
import java.util.Enumeration;
import java.util.logging.LogManager;
import java.util.logging.Logger;

/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
@SuppressWarnings("UnusedDeclaration")
public class IsolatingLogManager extends LogManager {
    private LogManager getDelegate() {
        Context context = ContextIsolator.getInstance().getContext();
        return context.getLogManagerWrapper().getManager();
    }

    @Override
    public void addPropertyChangeListener(PropertyChangeListener l) throws SecurityException {
        getDelegate().addPropertyChangeListener(l);
    }

    @Override
    public void removePropertyChangeListener(PropertyChangeListener l) throws SecurityException {
        getDelegate().removePropertyChangeListener(l);
    }

    @Override
    public boolean addLogger(Logger logger) {
        return getDelegate().addLogger(logger);
    }

    @Override
    public Logger getLogger(String name) {
        return getDelegate().getLogger(name);
    }

    @Override
    public Enumeration<String> getLoggerNames() {
        return getDelegate().getLoggerNames();
    }

    @Override
    public void readConfiguration() throws IOException, SecurityException {
        getDelegate().readConfiguration();
    }

    @Override
    public void reset() throws SecurityException {
        getDelegate().reset();
    }

    @Override
    public void readConfiguration(InputStream ins) throws IOException, SecurityException {
        getDelegate().readConfiguration(ins);
    }

    @Override
    public String getProperty(String name) {
        return getDelegate().getProperty(name);
    }

    @Override
    public void checkAccess() throws SecurityException {
        getDelegate().checkAccess();
    }
}
