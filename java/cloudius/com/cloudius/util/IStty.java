package com.cloudius.util;

public interface IStty {
    public void raw();
    public void reset();
    public void close() throws Exception;
}
