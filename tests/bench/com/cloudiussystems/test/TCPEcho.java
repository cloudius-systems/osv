/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

package com.cloudiussystems.test;

import java.net.*;

public class TCPEcho {

	public static void main(String[] args) throws java.io.IOException {
		int port = 7;
		if (args.length > 0) {
			port = Integer.parseInt(args[0]);
		}
		ServerSocket listener = new ServerSocket(port);
		while (true) {
			final Socket s = listener.accept();
			new Thread(new Runnable() {
				public void run() {
					echo(s);
				}
			}).start();
		}
	}
	
	private static void echo(Socket s) {
		byte[] b = new byte[8192];
		int r;
		try {
			while ((r = s.getInputStream().read(b)) != -1) {
				s.getOutputStream().write(b, 0, r);
			}
			s.close();
		} catch (java.io.IOException e) {
			throw new RuntimeException(e);
		}
	}

}
