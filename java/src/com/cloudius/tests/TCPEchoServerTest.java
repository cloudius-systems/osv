package com.cloudius.tests;
import java.io.*;
import java.lang.reflect.*;
import java.net.*;

import sun.org.mozilla.javascript.*;
import sun.org.mozilla.javascript.annotations.JSConstructor;
import sun.org.mozilla.javascript.annotations.JSFunction;

public class TCPEchoServerTest extends ScriptableObject {
    
    private TCPEchoServer _server;
    private TCPEchoClient _client;
    private int _port = 4444;
    private String _host = "127.0.0.1";
    private String _message = "TCP Echo Message!";
    private boolean _server_ok = false;
    private boolean _client_ok = false;
    
    /////////////////////
    // TCP Echo Server //
    /////////////////////
    
    public class TCPEchoServer extends Thread {
        
        public void run() {
            try {
                
                System.out.println("[~] Server: creating socket");
                ServerSocket ss = new ServerSocket();
                ss.bind(new InetSocketAddress(_port));

                System.out.println("[~] Server: calling accept...");
                Socket cs = ss.accept();
                
                System.out.println("[~] Server: new connection!");
                
                BufferedReader in = new BufferedReader( 
                        new InputStreamReader(cs.getInputStream()));
                BufferedWriter out = new BufferedWriter(
                        new OutputStreamWriter(cs.getOutputStream()));
                
                System.out.println("[~] Server: reading message...");
                char[] buf = new char[512];
                in.read(buf);
                
                System.out.println("[~] Server: echoing back...");
                out.write(buf);
                out.flush();
                
                // Clean up...
                System.out.println("[~] Server: closing connection");
                out.close(); 
                in.close();
                cs.close(); 
                ss.close();
                
                _server_ok = true;
            } catch (Exception ex) {
                System.out.println("[-] Exception in TCPEchoServer!");
                System.out.println(ex.getMessage());
                ex.printStackTrace();
            }
        }
    }
    
    /////////////////////
    // TCP Echo Client //
    /////////////////////
    
    public class TCPEchoClient extends Thread {
        
        public void run() {
            try {
                System.out.println("[~] Client: connecting to server...");
                Socket s = new Socket(InetAddress.getByName(_host), _port);
                s.setSoTimeout(2000);
                
                BufferedReader in = new BufferedReader( 
                        new InputStreamReader(s.getInputStream()));
                BufferedWriter out = new BufferedWriter(
                        new OutputStreamWriter(s.getOutputStream()));
                
                System.out.println("[~] Client: writing message...");
                out.write(_message);
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
                
                _client_ok = true;
            } catch (Exception ex) {
                System.out.println("[-] Exception in TCPEchoClient!");
                System.out.println(ex.getMessage());
                ex.printStackTrace();
            }
        }
    }
    
    private static final long serialVersionUID = 55505548787335642L;

    @Override
    public String getClassName() {
        return "TCPEchoServerTest";
    }
    
    @Override
    public Object getDefaultValue(Class<?> typeHint) {
        return toString();
    }

    @JSConstructor
    public TCPEchoServerTest() { }

    @JSFunction
    public boolean run() {
        
        try {
            _server = new TCPEchoServer();
            _client = new TCPEchoClient();
            
            _server.start();
            Thread.sleep(100);
            _client.start();
            
            _server.join();
            _client.join();
        } catch (Exception ex) {
            ex.printStackTrace();
        }
        
        return (_client_ok && _server_ok);
    }

}
