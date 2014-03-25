package io.osv.util.json;

/*
 * Copyright (C) 2013-2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

/**
 * A helper class to create array representative and handle the comma before
 * values
 * 
 */
public class ArrayBuilder {
	private boolean first = true;
	private StringBuilder sb = new StringBuilder();
	private String close = "]";

	/**
	 * The default constructor is used when the surrounding chars are square
	 * braces
	 */
	public ArrayBuilder() {
		sb.append("[");
	}

	/**
	 * A constructor that set an open and close characters
	 * 
	 * @param open
	 *            the open characters
	 * @param close
	 *            the close characters
	 */
	public ArrayBuilder(String open, String close) {
		this.close = close;
		sb.append(open);
	}

	/**
	 * Append a value
	 * 
	 * @param val
	 *            the value to add
	 */
	public StringBuilder append(String val) {
		if (first) {
			first = false;
		} else {
			sb.append(", ");
		}
		return sb.append(val);
	}

	/**
	 * Close the array and get a string out of it.
	 * 
	 * @return a string of the array
	 */
	public String toString() {
		sb.append(close);
		return sb.toString();
	}
}
