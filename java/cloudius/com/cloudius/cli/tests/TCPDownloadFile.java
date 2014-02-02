package com.cloudius.cli.tests;

import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.security.MessageDigest;

import com.cloudius.util.MD5;

public class TCPDownloadFile implements Test {

    public static final String _url = "http://mirror.isoc.org.il/pub/fedora/releases/18/Fedora/x86_64/iso/Fedora-18-x86_64-netinst.iso";
    public static final int _chunk_size = 0x10000;
    public static final String _expected_md5 = "227acebbc5392a4600349ae0c2d0ffcf";
    public static final int _max_iterations = 5;
    
    private boolean test() {
        try {
            URL url = new URL(_url);
            HttpURLConnection conn = (HttpURLConnection)url.openConnection();
            int rc = conn.getResponseCode();
            
            if (rc != HttpURLConnection.HTTP_OK) {
                System.out.format("error: HTTP response code %d, not 200\n", rc);
                return (false);
            }
            
            int totlen = conn.getContentLength();
     
            InputStream is = conn.getInputStream();
            MessageDigest m = MessageDigest.getInstance("MD5");
            m.reset();

            System.out.println("Begining file download...");
            
            int bytes=0;
            int total_bytes = 0;
            byte[] buf = new byte[_chunk_size];
            do {
                bytes = is.read(buf);
                total_bytes += bytes;
                System.out.format("\rDownloaded %.2f%%", 
                        ((float)total_bytes / (float)totlen)*100);
                if (bytes > 0) {
                    m.update(buf, 0, bytes);
                }
            } while (bytes != -1);
 
            System.out.println("\nFinished downloading");
            
            is.close();
            conn.disconnect();
            byte[] digest = m.digest();
            
            String dl_md5 = MD5.toHex(digest);
            System.out.format("Downloaded md5: %s\n", dl_md5);
            System.out.format("Expected md5: %s\n", _expected_md5);
            
            if (!dl_md5.equals(_expected_md5)) {
                System.out.println("error: md5 does not match!");
                return (false);
            } else {
                System.out.println("success: md5 match!");
            }
            
        } catch (Exception ex) {
            ex.printStackTrace();
            return (false);
        }
        
        return (true);
    }
    
    @Override
    public boolean run() {
        
        for (int i=0; i < _max_iterations; i++) {
            System.out.format("Running iteration %d/%d\n", i+1,
                    _max_iterations);
            boolean rc = this.test();
            if (!rc) {
                return (false);
            }
        }
        
        return (true);
    }
    

}
