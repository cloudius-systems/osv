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
