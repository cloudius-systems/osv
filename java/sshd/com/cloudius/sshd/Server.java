/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

package com.cloudius.sshd;

import java.io.IOException;
import java.util.ArrayList;

import org.apache.sshd.SshServer;
import org.apache.sshd.common.NamedFactory;
import org.apache.sshd.server.Command;
import org.apache.sshd.server.PasswordAuthenticator;
import org.apache.sshd.server.command.ScpCommandFactory;
import org.apache.sshd.server.keyprovider.SimpleGeneratorHostKeyProvider;
import org.apache.sshd.server.session.ServerSession;
import org.apache.sshd.server.sftp.SftpSubsystem;

public class Server {
    public static final String DEFAULT_PORT = "22";
    public static final String DEFAULT_HOSTKEY = "/etc/hostkey.est";
    public static final String DEFAULT_CONSOLE_CLI = "/console/cli.js";

    public static final String SYSTEM_PREFIX = "com.cloudius.sshd.";

    public static final String SYSTEM_PROP_PORT = SYSTEM_PREFIX + "port";
    public static final String SYSTEM_PROP_HOSTKEY = SYSTEM_PREFIX + "hostkey";
    public static final String SYSTEM_PROP_CONSOLE_CLI = SYSTEM_PREFIX
            + "console.cli";

    public static void main(String[] args) throws IOException {
        SshServer sshServer = SshServer.setUpDefaultServer();

        sshServer.setPort(Integer.parseInt(System.getProperty(SYSTEM_PROP_PORT,
                DEFAULT_PORT)));

        sshServer.setKeyPairProvider(new SimpleGeneratorHostKeyProvider(System
                .getProperty(SYSTEM_PROP_HOSTKEY, DEFAULT_HOSTKEY)));

        // Support SCP
        sshServer.setCommandFactory(new ScpCommandFactory());

        // Support SFTP subsystem
        ArrayList<NamedFactory<Command>> subsystemFactoriesList = new ArrayList<NamedFactory<Command>>(
                1);
        subsystemFactoriesList.add(new SftpSubsystem.Factory());
        sshServer.setSubsystemFactories(subsystemFactoriesList);

        // Set shell factory to Rhino
        sshServer.setShellFactory(new RhinoShellFactory(System.getProperty(
                SYSTEM_PROP_CONSOLE_CLI, DEFAULT_CONSOLE_CLI)));

        // Basic non-existend authentication
        sshServer.setPasswordAuthenticator(new PasswordAuthenticator() {
            @Override
            public boolean authenticate(String arg0, String arg1,
                    ServerSession arg2) {
                // TODO Implement authentication
                return true;
            }
        });

        sshServer.start();
    }
}
