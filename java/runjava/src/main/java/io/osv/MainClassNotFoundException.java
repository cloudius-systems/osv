package io.osv;

/*
 *   Copyright (C) 2014 Jaspal Singh Dhillon
 *
 *   This work is open source software, licensed under the terms of the
 *   BSD license as described in the LICENSE file in the top-level directory.
 */

public class MainClassNotFoundException extends Exception {

    private String mainClassName;

    public MainClassNotFoundException(String mainClass) {
	mainClassName = mainClass;
    }

    public String getClassName() {
        return mainClassName;
    }
}
