package com.cloudius.sshd;

import java.io.InputStream;
import java.io.OutputStream;

import org.apache.sshd.server.Command;
import org.apache.sshd.server.Environment;
import org.apache.sshd.server.ExitCallback;

import com.cloudius.cli.main.RhinoCLI;

public class RhinoCLICommand implements Command {

    private Thread cliThread;
    final Tty tty = new Tty();

    public RhinoCLICommand(String cliJs) {
        this.cliThread = new RhinoCLIThread();
    }

    class RhinoCLIThread extends Thread {
        @Override
        public void run() {
            String[] args = {};
            RhinoCLI.run(tty, false, args);
        }
    }

    @Override
    public void start(Environment env) {
        cliThread.start();
    }

    @Override
    public void setOutputStream(OutputStream out) {
        tty.setOut(out);
    }

    @Override
    public void setInputStream(InputStream in) {
        tty.setIn(in);
    }

    @Override
    public void setExitCallback(ExitCallback callback) {
    }

    @Override
    public void setErrorStream(OutputStream err) {
        tty.setErr(err);
    }

    @Override
    public void destroy() {
        cliThread.interrupt();
    }

}
