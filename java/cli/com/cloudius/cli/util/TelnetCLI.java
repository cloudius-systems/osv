package com.cloudius.cli.util;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetAddress;

import com.cloudius.cli.main.RhinoCLI;

public class TelnetCLI extends TelnetD {
    
    public TelnetCLI() throws IOException {
        super();
    }

    @Override
    protected void handleConnection(InputStream in, OutputStream out,
            InetAddress addr) throws IOException {
        System.err.println("Got connection from "+addr.getHostAddress());
        RhinoCLI.run(in, out, new String[0]);
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
