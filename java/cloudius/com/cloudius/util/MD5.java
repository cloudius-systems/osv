package com.cloudius.util;

import java.io.FileInputStream;
import java.security.MessageDigest;

public class MD5 {
    
    public static String toHex(byte[] bytes) {
        final char[] hexArray = {'0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f'};
        char[] hexChars = new char[bytes.length * 2];
        int v;
        for ( int j = 0; j < bytes.length; j++ ) {
            v = bytes[j] & 0xFF;
            hexChars[j * 2] = hexArray[v >>> 4];
            hexChars[j * 2 + 1] = hexArray[v & 0x0F];
        }
        return new String(hexChars);
    }
    
    public static String md5(String filename) {
        try {
            FileInputStream fi = new FileInputStream(filename);
            MessageDigest m = MessageDigest.getInstance("MD5");
            m.reset();
            
            int chunk_sz = 0x1000;
            byte bytes[] = new byte[chunk_sz];
            
            int len = chunk_sz; 
            while (len == chunk_sz) {
                len = fi.read(bytes);
                m.update(bytes, 0, len);
            }
            fi.close();
            
            // Calculate digest
            byte[] digest = m.digest();
            return (toHex(digest));
            
        } catch (Exception ex){
            System.out.println("md5() error:");
            ex.printStackTrace();
            return (null);
        }
    }
}
