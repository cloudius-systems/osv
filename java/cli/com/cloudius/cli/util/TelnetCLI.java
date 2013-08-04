package com.cloudius.cli.util;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetAddress;

import com.cloudius.cli.main.RhinoCLI;
import com.cloudius.cli.main.RhinoCLI.TTY;
import com.cloudius.util.IStty;

public class TelnetCLI extends TelnetD {
    
    public TelnetCLI() throws IOException {
        super();
    }

    @Override
    protected void handleConnection(InputStream in, OutputStream out,
            InetAddress addr) throws IOException {
        System.err.println("Got connection from "+addr.getHostAddress());
        RhinoCLI.run(new TelnetTTY(in, out), false, new String[0]);
    }
    
    private static class TelnetTTY implements TTY {
        // TODO: If we want to support "cooked" mode, to give
        // normal Unix-like behavior while a CLI command is running,
        // because OSV doesn't have ptys we'll need to implement a
        // line discipline (i.e., line editor) in a Java thread.
        // Right now we just always stay in raw mode.
        private static class TelnetStty implements IStty {
            public void raw() { }
            public void reset() { }
            public void close() throws Exception { }
        }
        private InputStream in;
        private OutputStream out;
        private TelnetStty stty;
        public TelnetTTY(InputStream in, OutputStream out) {
            this.in = in;
            this.out = out;
            this.stty = new TelnetStty();
        }
        
        public InputStream getIn() {
            return in;
        }
        public OutputStream getOut() {
            return out;
        }
        public IStty getStty() {
            return stty;
        }
    }
    
    public static void main(String[] args) {
        try {
            TelnetCLI server = new TelnetCLI();
            server.run();
            //new Thread(server).start();
        } catch(Throwable e) {
            e.printStackTrace();
        }

    }

}
