/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

package com.cloudius.sshd;

import org.apache.sshd.common.Factory;
import org.apache.sshd.server.Command;

public class RhinoShellFactory implements Factory<Command> {
    String cliJs;

    public RhinoShellFactory(String cliJs) {
        this.cliJs = cliJs;
    }

    @Override
    public Command create() {
        return new RhinoCLICommand(cliJs);
    }

}
