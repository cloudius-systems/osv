package tests;

/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
public class ClassFinder {
    public static void main(String[] args) throws ClassNotFoundException {
        String className = args[0];
        Class.forName(className);
    }
}
