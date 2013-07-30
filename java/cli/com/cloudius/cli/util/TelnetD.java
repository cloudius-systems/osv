package com.cloudius.cli.util;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.net.Socket;

// This is a simple Telnet server, which spawns a new thread for every
// connection, handles the telnet protocol exchanges (see RFC 854 and RFC 855)
// on that connection, and passes to a handleConnection() function defined by
// a subclass the clean input and output byte stream.
//
// This implementation is not only simple, it is also *simplistic*:
// Telnet, a 40 year old protocol, was designed to accommodate a wide range of
// archaic terminal types (including teleprinters), and wide range of network
// speeds, and has a huge number of operating options which the client and
// server negotiate to decide on various issues such as local echo, which
// side is in charge of line editing, and much much more. Currently we do not
// implementation we do not fully implement the negotiation protocol, and
// just spew out our side of the negotiation, assuming we're negotiating with
// a modern telnet client such as on Linux. To support a wider range of telnet
// clients, we may need in the future to add real option negotiation (i.e.,
// look at the options proposed by the client, and build a response based on
// these proposals - not a canned response like we do now).

public abstract class TelnetD implements Runnable {
    // A TelnetD server always has one socket on which it listens for
    // incoming requests
    private ServerSocket listeningSocket;

    // Constructors:

    public TelnetD(ServerSocket listeningSocket) {
        this.listeningSocket = listeningSocket;
    }
    // bindAddr is optional - if it is null, all IP addresses are bound.
    public TelnetD(int port, InetAddress bindAddr) throws IOException {
        this(new ServerSocket(port, -1, bindAddr));
    }
    // Default constructor - listen on port 23, on every interface.
    public TelnetD() throws IOException {
        this(23, null);
    }
    

    // After constructing a TelnetD-based object, run() should be called
    // to start the server. run() is part of the Runnable interface.
    @Override
    public void run() {
        System.err.println("Running telnet server");
        for (;;) {
            try {
                final Socket connection = listeningSocket.accept();
                // Handle each connection in a separate thread:
                new Thread() {
                    public void run() {
                        try {
                            handleConnection(
                                    new TelnetInputStream(connection.getInputStream()),
                                    new TelnetOutputStream(connection.getOutputStream()),
                                    connection.getInetAddress());
                        } catch (IOException e) {
                            // Empty block
                        } finally {
                            try {
                                connection.close();
                            } catch (IOException e) {
                                // Empty block
                            }
                        }
                    }
                }.start();
            } catch (IOException e) {
                // Empty block
            }
        }
    }
    
    // A subclass should override this function to define what it wants to run
    // in the connection. See for example TelnetCLI, which runs our CLI shell
    // for each connection. The "in" and "out" streams that handleConnection()
    // gets are clean user data, without the telnet protocol requests.
    protected abstract void handleConnection(InputStream in, OutputStream out,
            InetAddress addr) throws IOException;
    
    // Some Telnet protocol constants. See RFC 854.
    public static final int TELNET_IAC  = 255;
    public static final int TELNET_DONT = 254;
    public static final int TELNET_DO   = 253;
    public static final int TELNET_WONT = 252;
    public static final int TELNET_WILL = 251;
    public static final int TELNET_SB   = 250;
    public static final int TELNET_GA   = 249;
    public static final int TELNET_EL   = 248;
    public static final int TELNET_EC   = 247;
    public static final int TELNET_AYT  = 246;
    public static final int TELNET_AO   = 245;
    public static final int TELNET_IP   = 244;
    public static final int TELNET_BRK  = 243;
    public static final int TELNET_DM   = 242;
    public static final int TELNET_NOP  = 241;
    public static final int TELNET_SE   = 240;
    
    // See https://www.iana.org/assignments/telnet-options/telnet-options.xhtml
    // for telnet option negotiation protocols:
    
    // RFC 1184 - "Telnet Linemode Option":
    public static final int TELNET_OPTION_LINE_MODE = 34;
    public static final int TELNET_LINEMODE_MODE = 1;
    public static final int TELNET_LINEMODE_EDIT  = 1;
    public static final int TELNET_LINEMODE_TRAPSIG  = 2;
    public static final int TELNET_LINEMODE_ACK  = 4;
    public static final int TELNET_LINEMODE_SOFTTAB  = 8;
    public static final int TELNET_LINEMODE_LITECHO  = 16;
    // RFC 857 - "Telnet Echo Option":
    public static final int TELNET_OPTION_ECHO = 1;
  
    // TelentInputStream/TelnetOutputStream are wrappers for the socket's
    // input and output stream, which swallow some telnet protocol processing
    // and only pass through to read()/write() the actual user data.
    static class TelnetInputStream extends InputStream {
        private InputStream is;
        
        public TelnetInputStream(InputStream is) {
            this.is = is;
        }

        @Override
        public int read() throws IOException {
            int c = is.read();
            if (c == '\r') {
                // RFC 854 specifies that following '\r' we have either '\n',
                // in which case this is a newline, or a null (in which case
                // this is a bare carriage-return).
                c = is.read();
                switch(c) {
                case '\n': return '\r'; // Our CLI expect "\r" for newline
                case '\0': return '\r';
                default: throw new IOException("unexpected after carriage-return");
                }
            } else
            while (c == TELNET_IAC) {
                // Telnet command
                c = is.read();
                switch (c) {
                    case TELNET_WILL:
                    case TELNET_WONT:
                    case TELNET_DO:  
                    case TELNET_DONT:
                        // These four commands are followed by an option byte,
                        // which we read and currently ignore (as explained
                        // above, our negotiation is currently canned, and we
                        // the client's negotiation options).
                        c = is.read();
                        break;
                    case TELNET_SB:
                        // Subnegotiation starts with SB, and ends with SE.
                        // Again, we currently ignore it, but the right thing
                        // is to read and ack the options (esp. for RFC 1184).
                        while (c != TELNET_SE)
                            c = is.read();
                        break;
                    default:
                        // TODO: see if we need to support more commands
                }
                c = is.read();
            }
            return c;
        }
        
        @Override
        public void close() throws IOException {
            is.close();
        }
    }
    
    static class TelnetOutputStream extends OutputStream {
        private OutputStream os;
        int lastc = 0;
        
        public TelnetOutputStream(OutputStream os) throws IOException {
            this.os = os;
            
            // As explained above, this is a "canned" negotiation - we play
            // here our part in the negotiation assuming how the client side
            // will negotiate similarly to a modern telnet client on Linux.
            // 
            // Linux's telnet client wants to switch to remote-editing mode.
            // Tell it to go ahead with this negotiation, otherwise it will
            // remain in local-editing ("cooked") mode. The client will then
            // send an SB command (see RFC 1184), which we won't bother to
            // answer because the client will assume it was successful :-)
            os.write(TELNET_IAC);
            os.write(TELNET_DO);
            os.write(TELNET_OPTION_LINE_MODE);

            // Unless Linux's telnet client knows the server is responsible
            // for echo, it will also do its own echo.
            os.write(TELNET_IAC);
            os.write(TELNET_WILL);
            os.write(TELNET_OPTION_ECHO);
            
            os.flush();
        }

        @Override
        public void write(int c) throws IOException {
             if (lastc != '\r' && c == '\n')
                os.write('\r');
            os.write(c);
            if (lastc == '\r' && c != '\n')
                os.write('\0'); // RFC 854 specifies this
           lastc = c;
        }

        @Override
        public void flush() throws IOException {
            os.flush();
        }
        
        @Override
        public void close() throws IOException {
            os.close();
        }

    }
}
