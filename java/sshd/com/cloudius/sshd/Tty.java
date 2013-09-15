/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

package com.cloudius.sshd;

import java.io.InputStream;
import java.io.OutputStream;

import com.cloudius.cli.main.RhinoCLI;

public class Tty implements RhinoCLI.TTY {
    InputStream in;
    OutputStream out;
    OutputStream err;
    Stty stty = new Stty();

    public InputStream getIn() {
        return in;
    }

    public void setIn(InputStream in) {
        this.in = in;
    }

    public OutputStream getOut() {
        return out;
    }

    public void setOut(OutputStream out) {
        this.out = out;
    }

    public OutputStream getErr() {
        return err;
    }

    public void setErr(OutputStream err) {
        this.err = err;
    }

    public Stty getStty() {
        return stty;
    }
}
