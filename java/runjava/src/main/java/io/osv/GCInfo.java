package io.osv;
/*
 * Copyright (C) 2013-2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */


/**
 * 
 * A helper class that holds Garbage collection information
 *
 */
public class GCInfo {
	long count;
	long time;
	String name;
	String[] pools;
}
