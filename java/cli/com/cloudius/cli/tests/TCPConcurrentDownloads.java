/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

package com.cloudius.cli.tests;

import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.concurrent.CountDownLatch;

import com.cloudius.util.MD5;

public class TCPConcurrentDownloads implements Test {

    public static final String _url = "http://mirror.isoc.org.il/pub/fedora/releases/18/Fedora/x86_64/iso/Fedora-18-x86_64-netinst.iso";
    public static final int _chunk_size = 0x10000;
    public static final String _expected_md5 = "227acebbc5392a4600349ae0c2d0ffcf";
    public static final int _max_threads = 4;
    
    public class TCPDownloadFileThread extends Thread {
            
        public boolean test() {
            try {
                URL url = new URL(_url);
                HttpURLConnection conn = (HttpURLConnection)url.openConnection();
                int rc = conn.getResponseCode();
                
                if (rc != HttpURLConnection.HTTP_OK) {
                    System.out.format("[%d] error: HTTP response code %d, not 200\n", tid, rc);
                    return (false);
                }
                
                int totlen = conn.getContentLength();
         
                InputStream is = conn.getInputStream();
                MessageDigest m = MessageDigest.getInstance("MD5");
                m.reset();
    
                System.out.format("[%d] Begining file download...\n", tid);
                
                int chunks=0;
                int bytes=0;
                int total_bytes = 0;
                byte[] buf = new byte[_chunk_size];
                do {
                    bytes = is.read(buf);
                    if (bytes > 0) {
                        total_bytes += bytes;
                        m.update(buf, 0, bytes);
                    }
                    
                    chunks = chunks+1;
                    
                    if (chunks % 0x20 == 1) {
                        System.out.format("[%d] Downloaded %.2f%%\n", tid,
                                ((float)total_bytes / (float)totlen)*100);
                    }
                } while (bytes != -1);
     
                System.out.format("[%d] Downloaded 100.00%%\n", tid);
                System.out.format("[%d] Finished downloading\n", tid);
                
                is.close();
                conn.disconnect();
                byte[] digest = m.digest();
                
                String dl_md5 = MD5.toHex(digest);
                System.out.format("[%d] Downloaded md5: %s\n", tid, dl_md5);
                System.out.format("[%d] Expected md5: %s\n", tid, _expected_md5);
                
                if (!dl_md5.equals(_expected_md5)) {
                    System.out.format("[%d] error: md5 does not match!\n", tid);
                    return (false);
                } else {
                    System.out.format("[%d] success: md5 match!\n", tid);
                }
                
            } catch (Exception ex) {
                ex.printStackTrace();
                return (false);
            }
            
            return (true);
        }
        
        public int tid = 0;
        
        public void run() {
            boolean result = this.test();
            _results.set(tid-1, result);
            _latch.countDown();
        }
    }
    
    List<Boolean> _results = 
            Collections.synchronizedList(new ArrayList<Boolean>(_max_threads));
    public CountDownLatch _latch;
    
    @Override
    public boolean run() {
        _latch = new CountDownLatch(_max_threads);
        
        for (int i=0; i < _max_threads; i++) {
            _results.add(i, false);
            TCPDownloadFileThread t = new TCPDownloadFileThread();
            t.tid = i+1;
            t.start();
        }
        
        try {
            _latch.await();
        } catch (InterruptedException e) {
            e.printStackTrace();
        }
        
        for (Boolean b: _results) {
            if (!b) {
                return (false);
            }
        }
        
        return (true);
    }
    

}
