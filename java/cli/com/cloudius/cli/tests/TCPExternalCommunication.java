/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

package com.cloudius.cli.tests;

import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.net.InetAddress;
import java.net.Socket;

public class TCPExternalCommunication implements Test {

    @Override
    public boolean run() {
        try {
            System.out.println("[~] connecting to 173.194.70.113...");
            Socket s = new Socket(InetAddress.getByName("173.194.70.113"), 80);
            s.setSoTimeout(2000);
            
            BufferedReader in = new BufferedReader( 
                    new InputStreamReader(s.getInputStream()));
            BufferedWriter out = new BufferedWriter(
                    new OutputStreamWriter(s.getOutputStream()));
            
            System.out.println("[~] Client: writing message...");
            out.write("GET / HTTP/1.0\r\n\r\n");
            out.flush();
            
            System.out.println("[~] Client: reading response...");
            char[] buf = new char[512];
            in.read(buf);

            System.out.println("[~] Client: message received:");
            System.out.println(buf);
            
            System.out.println("[~] Client: closing connection");
            out.close(); 
            in.close();
            s.close();
            
            return (true);
        } catch (Exception ex) {
            System.out.println("[-] Exception in TCPEchoClient!");
            System.out.println(ex.getMessage());
            ex.printStackTrace();
            return (false);
        }
    }
    
    
}