package com.cloudius.cli.tests;

import java.io.FileOutputStream;
import java.net.URL;
import java.nio.channels.Channels;
import java.nio.channels.ReadableByteChannel;

public class TCPDownloadFile implements Test {

    @Override
    public boolean run() {
        try {
            URL website = new URL("http://212.58.244.66/");
            ReadableByteChannel rbc = Channels.newChannel(website.openStream());
            FileOutputStream fos = new FileOutputStream("/tmp/bbc.co.uk.html");
            fos.getChannel().transferFrom(rbc, 0, 1 << 24);
            fos.close();
        } catch (Exception ex) {
            ex.printStackTrace();
            return (false);
        }
        return (true);
    }
    

}
