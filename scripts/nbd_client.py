#!/usr/bin/env python
# -*- coding: utf-8 -*- 
# 
# Copyright (C) 2013 Nodalink, SARL.
#
# Simple nbd client used to connect to qemu-nbd
#
# author: Benoît Canet <benoit.canet@irqsave.net>
# 
# This work is open source software, licensed under the terms of the
# BSD license as described in the LICENSE file in the top-level directory.
#

from exceptions import ValueError
import socket
import struct

class nbd_client(object):

    READ = 0
    WRITE = 1
    DISCONNECT = 2
    FLUSH = 3

    FLAG_HAS_FLAGS = (1 << 0)
    FLAG_SEND_FLUSH = (1 << 2)

    def __init__(self, hostname, port = 10809):
        self._flushed = True
        self._closed = True
        self._is_read = False
        self._handle = 0
        self._length = 0
        self._s = socket.create_connection((hostname, port))
        self._closed = False
        self._old_style_handshake()

    def __del__(self):
        self.close()

    def close(self):
        if not self._flushed:
            self.flush()
        if not self._closed:
            self._disconnect()
            self._closed = True

    def _old_style_handshake(self):
        nbd_magic = self._s.recv(len("NBDMAGIC"))
        assert(nbd_magic == "NBDMAGIC")
        buf = self._s.recv(8 + 8 + 4)
        (magic, self._size, self._flags) = struct.unpack(">QQL", buf)
        assert(magic == 0x00420281861253)
        # ignore trailing zeroes
        self._s.recv(124)

    def _build_header(self, request_type, offset, length):
        self._is_read = False
        header = struct.pack('>LLQQL', 0x25609513,
                             request_type, self._handle, offset, length)
        return header

    def _parse_reply(self):
        data = ""
        reply = self._s.recv(4 + 4 + 8)
        (magic, errno, handle) = struct.unpack(">LLQ", reply)
        assert(magic == 0x67446698)
        assert(handle == self._handle)
        self._handle += 1
        if self._is_read:
            data = self._s.recv(self._length)
        return (data, errno)

    def _check_value(self, name, value):
        if not value % 512:
            return
        raise ValueError("%s=%i is not a multiple of 512" % (name, value))

    def write(self, data, offset):
        self._check_value("offset", offset)
        self._check_value("size", len(data))
        self._flushed = False
        self._is_read = False
        header = self._build_header(self.WRITE, offset, len(data))
        self._s.send(header + data)
        (data, errno) = self._parse_reply()
        assert(errno == 0)
        return len(data)

    def read(self, offset, length):
        self._check_value("offset", offset)
        self._check_value("length", length)
        header = self._build_header(self.READ, offset, length)
        self._is_read = True
        self._length = length
        self._s.send(header)
        (data, errno) = self._parse_reply()
        assert(errno == 0)
        return data

    def need_flush(self):
        if self._flags & self.FLAG_HAS_FLAGS != 0 and \
           self._flags & self.FLAG_SEND_FLUSH != 0:
            return True
        else:
            return False

    def flush(self):
        self._is_read = False
        if self.need_flush() == False:
            self._flushed = True
            return True
        header = self._build_header(self.FLUSH, 0, 0)
        self._s.send(header)
        (data, errno) = self._parse_reply()
        self._handle += 1
        if not errno:
            self._flushed = True
        return errno == 0

    def _disconnect(self):
        self._is_read = False
        header = self._build_header(self.DISCONNECT, 0, 0)
        self._s.send(header)

    def size(self):
        return self._size
