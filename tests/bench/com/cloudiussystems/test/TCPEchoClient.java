package com.cloudiussystems.test;

import java.net.*;

public class TCPEchoClient {

	public static void main(String[] args) throws java.io.IOException {
		final long bytes = Long.parseLong(args[0]);
		String host = args[1];
		int port = 7;
		if (args.length > 2) {
			port = Integer.parseInt(args[2]);
		}
		final Socket s = new Socket(host, port);
		new Thread(new Runnable() {
			public void run() {
				write_data(s, bytes);
			}
		}).start();
		read_data(s);
	}
	
	private static void write_data(Socket s, long bytes) {
		try {
			byte[] b = new byte[8192];
			long done = 0;
			while (done < bytes) {
				int n = (int)Math.min(b.length, bytes - done);
				s.getOutputStream().write(b, 0, n);
				done += n;
			}
			s.shutdownOutput();
		} catch(java.io.IOException e) {
			throw new RuntimeException(e);
		}
	}
	
	private static void read_data(Socket s) throws java.io.IOException {
		byte[] b = new byte[8192];
		while (s.getInputStream().read(b) != -1) {
			// nothing to do
		}
		s.close();
	}
	
}
