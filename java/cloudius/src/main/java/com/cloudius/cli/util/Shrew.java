/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

package com.cloudius.cli.util;


import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.UnsupportedEncodingException;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.net.URLDecoder;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Enumeration;
import java.util.Hashtable;
import java.util.Locale;
import java.util.Properties;
import java.util.StringTokenizer;
import java.util.TimeZone;

/**
 * @author Nadav Har'El
 *
 * Shrew - a tiny HTTP server for embedding in an application.
 *
 * Shrews are some of the smallest mammals in the world, and "Shrew" is one of
 * the smallest HTTP servers, just one Java file (this one). Its name is a pun
 * on the name of Tomcat, which is a much bigger animal :-) Shrew is *not* a
 * servlet engine. The subclass ShrewSE (Shrew Servlet Engine) implements the
 * servlet API on top of Shrew.
 *
 * A few more notes about Shrew's design and how to use it:
 *
 * 1. Shrew is a simple thread-spawning server: a new thread is spawned per HTTP
 * connection, and dies when the connection closes.
 *
 * 2. Shrew was designed to be embedded in an application that needs an HTTP
 * server inside it. For the application to control what Shrew serves, the
 * "Shrew" class should be subclassed and the processRequest() method overriden.
 * This processReques() will typically call serveFile() to serve files,
 * serveString() to serve strings (like errors and dynamic content), and use
 * htmlFormParameters() to parse parameters. A Shrew subclass may also want to
 * override the serveError() class, to write error pages more suited to the
 * application's style. An example subclass is ShrewSE, which implements a
 * Servlet Engine using Shrew.
 *
 * 3. A Shrew server is run by creating an object of type Shrew (or its
 * subclass), and invoking its run() method. Alternatively, one can create a
 * Thread from this Shrew (which is a Runnable), and use it to start the
 * server's main loop in a separate thread.
 *
 * 4. Shrew is meant to be simple, so it is an HTTP 0.9 server and may not
 * support advanced HTTP features that are not very important for an
 * application-embedded Web server. This shouldn't cause any problems when
 * serving to standard browsers, However.
 *
 * 5. Shrew is a Java class, and the application communicates with it using Java
 * strings, not byte arrays. Therefore, Shrew has to decide on an encoding to
 * use when reading and writing those strings to the network. The decision was
 * to use UTF-8. In particular, serveString() serves a string in UTF-8 encoding
 * and handleConnnection() assumes the query parameters are UTF-8 encoded (this
 * is normaly the case, when a browser replies to a form in an UTF-8 encoded
 * page).
 */
public class Shrew implements Runnable {
    // A Shrew server always has one socket on which it listens for
    // incoming requests
    private ServerSocket listeningSocket;

    // Constructors:
    public Shrew(ServerSocket listeningSocket) {
        this.listeningSocket = listeningSocket;
    }

    // bindAddr is optional - if it is null, all IP addresses are bound.
    public Shrew(int port, InetAddress bindAddr) throws IOException {
        this(new ServerSocket(port, -1, bindAddr));
    }

    // After constructing a Shrew-based object, run() should be called
    // to start the server. run() is part of the Runnable interface.
    public void run() {
        for (;;) {
            try {
                final Socket connection = listeningSocket.accept();
                // Handle the connection in a separate thread:
                new Thread() {
                    public void run() {
                        try {
                            handleConnection(connection);
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
                } .start();
            } catch (IOException e) {
                // Empty block
            }
        }
    }

    // handleConnection() handles one HTTP connnection - it reads and
    // parses the request, and calls processRequest() to process it
    // and send a response.
    // This method is started in a separate thread, so it can do
    // whatever it wants, including blocking.
    private void handleConnection(Socket connection) throws IOException {
        final BufferedInputStream in = new BufferedInputStream(connection
                .getInputStream());

        // read and parse the request line (RFC 2616, section 5.1)
        // It includes 3 parts - Method, Request-URL and HTTP-Version.
        final String requestLine = readHTTPline(in);
        String method, requestURI, HTTPVersion;
        final int space1 = requestLine.indexOf(' ');
        if (space1 < 0) {
            method = requestLine;
            requestURI = "";
            HTTPVersion = "";
        } else {
            method = requestLine.substring(0, space1);
            final int space2 = requestLine.indexOf(' ', space1 + 1);
            requestURI = space2 < 0 ? requestLine.substring(space1 + 1)
                         : requestLine.substring(space1 + 1, space2);
            HTTPVersion = space2 < 0 ? "" : requestLine.substring(space2 + 1);
        }

        // RFC 2616 section 5.1.2 specifies that we should percent-decode
        // the URI now. However, Apache actually seems to do something
        // different: first split the URI into path and query separated
        // by a "?" character, and then percent-decode each individually.
        // This allows a file whose name contains a "?" to be served
        // (%3f is used in the URI). Leaving the query string undecoded
        // even longer allows the parameters to contain characters
        // (encoded) like & and =, and Tomcat for example does this.
        // RFC 3986 (URIs) appears to agree with this interpretation,
        // and we'll do it too.
        String path, query;
        final int questionmark = requestURI.indexOf('?');
        if (questionmark < 0) {
            path = percentDecode(requestURI);
            query = null;
        } else {
            path = percentDecode(requestURI.substring(0, questionmark));
            query = requestURI.substring(questionmark + 1);
        }

        // read and parse the request headers. See RFC 2616 section 4.2.
        // We can use a Properties object for headers, because the RFC
        // explicitly mentions that headers with the same name can be
        // combined (using a comma).
        // TODO: support the line continuation explained in RFC 2616 section 2.2
        Properties requestHeaders = new Properties();
        for (;;) {
            final String line = readHTTPline(in);
            int colon = line.indexOf(':');
            if (colon < 0)
                break; // end of headers, or malformed header.
            // header names are case insensitive, so fold them to lowercase
            final String name = line.substring(0, colon).toLowerCase();
            // value may be preceded by any number of LWS (SP or HT) -
            // skip it
            for (colon++; colon < line.length(); colon++)
                if (line.charAt(colon) != ' ' && line.charAt(colon) != '\t')
                    break;
            String value = line.substring(colon);
            String oldvalue = requestHeaders.getProperty(name);
            if (oldvalue == null)
                requestHeaders.setProperty(name, value);
            else
                requestHeaders.setProperty(name, oldvalue + "," + value);
        }

        // Let the processRequest() function, most likely overriden by
        // a subclass, process this request and send the results
        BufferedOutputStream out = new BufferedOutputStream(connection
                .getOutputStream());

        System.err.println("Shrew " + connection.getInetAddress().getHostAddress() //getHostName()
                           + " " + method + " " + path
                           + (query == null ? "" : ("?" + query)));
        processRequest(method, path, query, requestHeaders, in, out,
                       connection.getInetAddress(), connection.getPort(),
                       connection.getLocalAddress(), connection.getLocalPort(),
                       HTTPVersion);

        out.close();
        in.close();
    }

    /**
     * This is just an example implementation, implementing a simple file
     * server. Users of shrew will probably subclass this class and replace the
     * implementation of processRequest.
     * The "in" input stream is positioned after the headers, and the request
     * content (e.g., POST parameters) can be read from it. Please note that
     * if a Content-Length header exists, only a maximum of this number of
     * bytes should be read from the input stream (if you attempt to read
     * more, the read may hang, waiting for input that will never arrive).
     * The "out" output stream is used to write out the response -
     * usually by using the serveString() and serveFile() routines. You do not
     * have to close or flush the output stream at the end of this function, as
     * the caller (handleConnection()) does it.
     */
    protected void processRequest(String method, String path, String query,
                                  Properties requestHeader, InputStream in, OutputStream out,
                                  InetAddress clientAddress, int clientPort,
                                  InetAddress localAddress, int localPort, String HTTPVersion)
    throws IOException {
        System.out.println("Got request: method=" + method + ", path=" + path
                           + ", query=" + query);
        final String root = "/";
        // serveString(out, 200, null,
        // "<HTML><BODY><H1>Hello</H1></BODY></HTML>","text/html");
        if (path.startsWith("..") || path.endsWith("..")
                || path.indexOf("../") >= 0 || path.indexOf("..\\") >= 0) {
            serveError(out, 403, "URL " + path + " contains '..'");
            return;
        }
        serveFile(out, new File(root, path));
    }

    /**
     * readHTTPline() is specific to the needs of the HTTP protocol: it reads a
     * line ending with CRLF. As RFC 2616 section 5.1 allows us to assume, no CR
     * or LF characters may appear in the line except in the final CRLF
     * sequence. As RFC 2616 section section 2.2 explains, the line can only
     * contain ASCII and perhaps ISO-8859-1, and for other encodings RFC 2047
     * can be used (but we do not support that at the moment)
     */
    private static String readHTTPline(InputStream in) throws IOException {
        StringBuffer sb = new StringBuffer();
        int c;
        while ((c = in.read()) >= 0 && c != '\n') {
            if (c != '\r') // can only be the CR before the LF
                sb.append((char) (byte) c);
        }
        return sb.toString();
    }

    /**
     * percentDecode decodes the URI encoding explained (among other places) in
     * RFC 3986: bytes are potentially encoded as "%" followed by two
     * hexadecimal digits.
     *
     * We have here an encoding problem. We are returning a Java String, not a
     * byte array. But to do that, we need to know what encoding was used to
     * generate the byte sequence we see in the URL. RFC 2616 section 2.2
     * actually specifies that this encoding should be specified exlicitly as
     * explained in RFC 2047, but unfortunately this standard does not appear to
     * be used by any browser. Instead, what browsers appear to be doing is to
     * use the same encoding they saw in the page in which a form was being
     * filled. Because we use the UTF-8 encoding in pages we serve, we will
     * therefore assume this encoding also in the request we get.
     * Two further comments:
     * 1) The HTML 4 standard, http://www.w3.org/TR/REC-html40/interact/forms.html
     *    specifies that the GET method restricts the form data to be ASCII, and
     *    only the "post" method with enctype="multipart/form-data" covers the
     *    entire unicode. Like I mentioned above, this is not true in current
     *    browsers.
     *  2) RFC 3986 (URI) recommends that for *new* URI schemes (i.e., perhaps
     *    not http, "the data should first be encoded as octets according to
     *    the UTF-8 character encoding; then only those octets that do not
     *    correspond to characters in the unreserved set should be percent-encoded."
     *    See also RFC 3987
     */
    private static String percentDecode(String str) {
        try {
            return URLDecoder.decode(str, "UTF-8");
        } catch (UnsupportedEncodingException e) {
            return str; // this shouldn't happen...
        } catch (IllegalArgumentException e) {
            return str; // malformed URL, probably...
        }
    }

    /**
     * serveString() writes out an HTTP response based on the given string,
     * whose mime-type is also given. We are writing out a Java String, not a
     * byte array, so the mime type must be a textual such as "text/plain",
     * "text/html" or "text/xml". The string is written out in UTF8 encoding,
     * and the relevant Content-Type: header is generated to say that (the
     * caller shouldn't put "content-type" in the given headers parameter).
     *
     * The moreheaders array is another way to specify response headers when
     * their order is important, or when it's important not to merge two headers
     * into one header with comma-separated values. In theory, there shouldn't
     * be a need for this (because RFC 2616 explicitly says that header order is
     * not important, and that two headers with the same name can be merged into
     * one by separating the values with a comma), but unfortunately, in reality
     * this is needed for Set-Cookie headers.
     */
    protected void serveString(OutputStream out, int statusCode,
                               Properties headers, String content, String contentType)
    throws IOException {
        serveString(out, statusCode, headers, null, content, contentType);
    }

    protected void serveString(OutputStream out, int statusCode,
                               Properties headers, String moreheaders[], String content,
                               String contentType) throws IOException {
        sendStatus(out, statusCode);
        sendHeader(out, "Content-Type", contentType + "; charset=UTF-8");
        // RFC 2616 section 14.18 says we must provide a Date header...
        if (headers == null || headers.getProperty("date") == null)
            sendHeader(out, "Date", HTTPdate(new Date()));
        // send the extra headers (see comment above on moreheaders)
        if (moreheaders != null)
            for (int i = 0; i < moreheaders.length; i++) {
                out.write((moreheaders[i] + "\r\n").getBytes());
            }
        sendHeaders(out, headers);
        out.write(content.getBytes("UTF-8"));
    }

    /**
     * serveFile() writes out the given file as an HTTP response. The file's
     * MIME-type is simplisticly determined from its extension.
     *
     * Note that serveFile() takes an absolute filename as parameter. It is up
     * to the caller to make sure that the user should really be allowed to see
     * this file (e.g., by making sure that this method is called only for files
     * inside some fixed root directory).
     */
    protected void serveFile(OutputStream out, File f) throws IOException {
        if (f.isDirectory()) {
            // TODO: check for a index.html inside the directory; also
            // consider redirecting if there is no slash in the end.
            // serveError(out, 403, "Directory listing not allowed.");
            StringBuilder html = new StringBuilder();
            html.append("<HTML><HEAD><TITLE>Directory "+f.getPath()+
                        "</TITLE></HEAD>\n<BODY><H1>Directory "+f.getPath()+
                        "</H1>\n<UL>\n");
            for (File file : f.listFiles()) {
                html.append("<LI><A HREF=\""+file.getPath()+"\">"+file.getName()+"</A></LI>");
            }
            html.append("</UL>\n</BODY></HTML>\n");
            serveString(out,  200, null, html.toString(), "text/html");
            return;
        }

        // Try to open the file...
        FileInputStream fis;
        try {
            fis = new FileInputStream(f);
        } catch (IOException e) {
            if (!f.exists()) {
                serveError(out, 404, "File " + f.getAbsolutePath()
                           + " does not exist on server");
            } else {
                serveError(out, 403, "File " + f.getAbsolutePath()
                           + " unreadable on server");
            }
            return;
        }

        // Guess the MIME type of the file from its extension...
        String type = null;
        final int dot = f.getAbsolutePath().lastIndexOf('.');
        if (dot >= 0)
            type = extensions.getProperty(f.getAbsolutePath()
                                          .substring(dot + 1).toLowerCase());
        if (type == null)
            type = "application/octet-stream"; // some default..

        // send OK status, and headers
        sendStatus(out, 200);
        sendHeader(out, "Date", HTTPdate(new Date()));
        sendHeader(out, "Last-Modified", HTTPdate(new Date(f.lastModified())));
        sendHeader(out, "Content-Type", type);
        sendHeaders(out, null);

        // send the file content itself
        final byte[] buf = new byte[1024];
        int len;
        while ((len = fis.read(buf)) > 0)
            out.write(buf, 0, len);
    }

    private static Properties extensions;
    static {
        extensions = new Properties();
        extensions.setProperty("html", "text/html");
        extensions.setProperty("htm", "text/html");
        extensions.setProperty("txt", "text/plain");
        extensions.setProperty("gif", "image/gif");
        extensions.setProperty("jpg", "image/jpg");
        extensions.setProperty("png", "image/png");
        extensions.setProperty("ico", "image/x-icon");
        extensions.setProperty("css", "text/css");
        extensions.setProperty("js", "text/javascript");
    }

    /**
     * serveError() is called to serve an error response. It takes a status code
     * (from which it can figure out a textual description of the error), and a
     * "moreinfo" string which is supposed to be an HTML string giving more
     * details about the cause of the problem.
     *
     * When Shrew is embedded in an application, it will often want to to
     * override this method, and generate error pages which are more aligned
     * with the look of the pages generated by the application.
     */
    protected void serveError(OutputStream out, int statusCode, String moreinfo)
    throws IOException {
        serveString(out, statusCode, null, "<HTML><HEAD><TITLE>" + statusCode
                    + " " + statusToReason(statusCode)
                    + "</TITLE></HEAD><BODY><H1>" + statusToReason(statusCode)
                    + "</H1>\n" + moreinfo + "</BODY></HTML>", "text/html");
    }

    /**
     * Convert an integer HTTP status code into a very short textual
     * description, or "reason phrase". We took the suggested reason phrases
     * from RFC 2616, section 6.1.1. But note that these strings are
     * informational only, and are not needed for the correct functioning of the
     * HTTP server.
     */
    protected static String statusToReason(int statusCode) {
        switch (statusCode) {
            // 1xx: Informational:
        case 100:
            return "Continue";
        case 101:
            return "Switching Protocols";
            // 2xx: Success:
        case 200:
            return "OK";
        case 201:
            return "Created";
        case 202:
            return "Accepted";
        case 203:
            return "Non-Authoritative Information";
        case 204:
            return "No Content";
        case 205:
            return "Reset Content";
        case 206:
            return "Partial Content";
            // 3xx: Redirection:
        case 300:
            return "Multiple Choices";
        case 301:
            return "Moved Permanently";
        case 302:
            return "Found";
        case 303:
            return "See Other";
        case 304:
            return "Not Modified";
        case 305:
            return "Use Proxy";
        case 307:
            return "Temporary Redirect";
            // 4xx: Client error:
        case 400:
            return "Bad Request";
        case 401:
            return "Unauthorized";
        case 402:
            return "Payment Required";
        case 403:
            return "Forbidden";
        case 404:
            return "Not Found";
        case 405:
            return "Method Not Allowed";
        case 406:
            return "Not Acceptable";
        case 407:
            return "Proxy Authentication Required";
        case 408:
            return "Request Time-out";
        case 409:
            return "Conflict";
        case 410:
            return "Gone";
        case 411:
            return "Length Required";
        case 412:
            return "Precondition Failed";
        case 413:
            return "Request Entity Too Large";
        case 414:
            return "Request-URI Too Large";
        case 415:
            return "Unsupported Media Type";
        case 416:
            return "Requested range not satisfiable";
        case 417:
            return "Expectation failed";
            // 5xx: Server error:
        case 500:
            return "Internal Server Error";
        case 501:
            return "Not Implemented";
        case 502:
            return "Bad Gateway";
        case 503:
            return "Service Unavailable";
        case 504:
            return "Gateway Time-Out";
        case 505:
            return "HTTP Version Not Supported";
        default:
            return "Unknown";
        }
    }

    /**
     * Send status line (RFC 2616, section 6.1)
     */
    private void sendStatus(OutputStream out, int statusCode)
    throws IOException {
        // NOTE: the encoding in getBytes() below doesn't matter: we assume
        // it is ASCII anyway.
        out
        .write(("HTTP/1.0 " + statusCode + " "
                + statusToReason(statusCode) + "\r\n").getBytes());
    }

    private void sendHeader(OutputStream out, String name, String value)
    throws IOException {
        // NOTE: the encoding in getBytes() below doesn't matter: we assume
        // it is ASCII anyway.
        out.write((name + ": " + value + "\r\n").getBytes());
    }

    private void sendHeaders(OutputStream out, Properties headers)
    throws IOException {
        if (headers != null)
            for (Enumeration e = headers.propertyNames(); e.hasMoreElements();) {
                String name = (String) e.nextElement();
                sendHeader(out, name, headers.getProperty(name));
            }
        // Following all the headers, should come a CRLF
        out.write('\r');
        out.write('\n');
    }

    /**
     * Format date in HTTP format, as explained in RFC 2616 section 14.18,
     * section 3.3.1, and ultimately, in RFC 1123. Such a date looks like "Sun,
     * 06 Nov 1994 08:49:37 GMT" (with GMT timezone).
     */
    private static String HTTPdate(Date date) {
        return HTTPdateFormatter.format(date);
    }

    private static SimpleDateFormat HTTPdateFormatter;
    static {
        HTTPdateFormatter = new SimpleDateFormat(
            "E, dd MMM yyyy HH:mm:ss 'GMT'", Locale.US);
        HTTPdateFormatter.setTimeZone(TimeZone.getTimeZone("GMT"));
    }

    /**
     * Don't call this function directly. Prefer htmlFormParameters().
     *
     * The method parseQueryString takes a query string such as the one passed
     * to the processQuery(), which contains key=value pairs separated by &
     * characters, and perhaps percent-encoded. This method puts these keys and
     * values (after decoding) in a hash table. There may be multiple values per
     * key, so the value for each key is an array of strings, rather than a
     * single string. The order of the values of a single key is preserved, but
     * the order between keys is not (because a hash table does not preserve
     * order). This method ignores format errors.
     */
    private static Hashtable parseQueryString(String query) {
        Hashtable<String, String[]> ht = new Hashtable<String, String[]>();
        if (query == null)
            return ht;
        StringTokenizer st = new StringTokenizer(query, "&");
        while (st.hasMoreTokens()) {
            String pair = st.nextToken();
            int eq = pair.indexOf('=');
            if (eq >= 0) {
                String key = percentDecode(pair.substring(0, eq));
                String value = percentDecode(pair.substring(eq + 1));
                if (ht.containsKey(key)) {
                    // This implementation is quite inefficient, but
                    // this won't bother us in any real-world use.
                    String[] oldvalues = (String[]) ht.get(key);
                    String[] values = new String[oldvalues.length + 1];
                    System.arraycopy(oldvalues, 0, values, 0, oldvalues.length);
                    values[oldvalues.length] = value;
                    ht.put(key, values);
                } else {
                    String[] values = new String[1];
                    values[0] = value;
                    ht.put(key, values);
                }
            }
        }
        return ht;
    }

    /**
     * This is a convenience function for Shrew subclasses, for processing
     * paramters sent by a browser from an HTML form. These parameters could
     * have been sent in one of several forms. For the GET method, the "query",
     * the part of the URI following a question mark, is used. The function
     * parseQueryString() above knows how to process this and extract the
     * key/value pairs. Alternatively, for the POST method, the content of the
     * request (read from "in") might contain such a query-string. For that, the
     * Content-type header must be "application/x-www-form-encoded". A third
     * alternative is POST with multipart/form-data, but we do not support it in
     * the current code. See the HTML 4.0 standard, section 17.3.4 ("Form
     * content types")
     * (http://www.w3.org/TR/REC-html40/interact/forms.html#form-content-type)
     * for more information. See important comment about HTML parameters'
     * encoding issues above percentDecode().
     */
    protected static Hashtable htmlFormParameters(String method, String query,
            Properties requestHeader, InputStream in) {
        if (method.equalsIgnoreCase("GET")) {
            return parseQueryString(query);
        } else if (method.equalsIgnoreCase("POST")) {
            final String ct = requestHeader.getProperty("content-type");
            final String cl = requestHeader.getProperty("content-length");
            if (ct != null
                    && ct.equalsIgnoreCase("application/x-www-form-urlencoded")) {
                try {
                    int len = 0;
                    if (cl != null)
                        len = Integer.parseInt(cl);
                    if (len > 4096)
                        len = 4096; // sanity limit...
                    byte[] line = new byte[len];
                    int nread = 0;
                    while (len > nread) {
                        int n = in.read(line, nread, len - nread);
                        if (n < 0)
                            break;
                        nread += n;
                    }
                    // The line is ASCII, so new String(line) is ok.
                    return parseQueryString(new String(line, 0, nread));
                } catch (Throwable e) {
                    // Empty block
                } /* fall through and return empty hashtable */
            }
            // TODO: handle also multipart/form-data
            // see RFC 1867
            // This will also make the file upload supported
        }

        // If we don't know what to do, return empty hash table.
        return new Hashtable();
    }

    public static void main(String args[]) {
        try {
            Shrew server = new Shrew(8080, null);
            if (args.length > 0 && args[0].equals("fg")) {
                System.out.println("Started HTTP server on port 8080, in foreground");
                server.run();
            } else {
                System.out.println("Started HTTP server on port 8080, in background");
                new Thread(server).start();
            }
        } catch(Throwable e) {
            e.printStackTrace();
        }
    }
}
