package io.osv;

import java.io.Closeable;
import java.io.IOException;
import java.security.Permission;

/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
class TemporarySecurityManager extends SecurityManager implements Closeable {
    TemporarySecurityManager() {
        System.setSecurityManager(this);
    }

    @Override
    public void checkPermission(Permission perm) {
        if (!perm.getName().equals("setSecurityManager")) {
            super.checkPermission(perm);
        }
    }

    @Override
    public void close() throws IOException {
        System.setSecurityManager(null);
    }
}
