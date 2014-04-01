#!/usr/bin/python

import sys, os, optparse, struct
import subprocess
import time

from nbd_client import nbd_client

_devnull = open('/dev/null', 'w')

cmd = sys.argv[1]
args = sys.argv[2:]

args_offset = 512

def chs(x):
    sec_per_track = 63
    heads = 255 

    c = (x // sec_per_track) // heads
    h = (x // sec_per_track) % heads
    s = x % sec_per_track + 1

    # see http://en.wikipedia.org/wiki/Master_Boot_Record
    if c > 1023:
        c = 1023
        h = 254
        s = 63

    return c,h,s

def read_chars_up_to_null(file):
    while True:
        try:
            c = file.read(1)
            if c == '\0':
                raise StopIteration
            yield c
        except ValueError:
            raise StopIteration

def read_cstr(file):
    return ''.join(read_chars_up_to_null(file))

def write_cstr(file, str):
    file.write(str.encode())
    file.write(b'\0')

class nbd_file(object):

    def __init__(self, filename):
        self._filename = filename
        self._offset = 0
        self._buf    = None
        self._closed = True
        self._process = subprocess.Popen("qemu-nbd %s" % filename,
                                        shell = True, stdout=_devnull)
        # wait for qemu-nbd to start: this thing doesn't tell anything on stdout
        while True:
            try:
                self._client = nbd_client("localhost")
                break
            except:
                if self._process.poll() != None:
                    raise Exception('Qemu terminated with exit code %d' % self._process.returncode)
                time.sleep(0.1)
        self._closed = False

    def __del__(self):
        self.close()

    def __enter__(self):
        return self

    def __exit__(self, type, value, traceback):
        self.close()

    def close(self):
        if self._closed:
            return
        # send disconnect to nbd server
        self._client.close()
        # wait for server to exit
        if self._process.wait():
            raise Exception('Qemu terminated with exit code %d' % self._process.returncode)
        self._closed = True

    def seek(self, offset):
        self._offset = offset

    def _sect_begin(self, offset):
        return (offset // 512) * 512

    def _offset_in_sect(self, offset):
        return offset % 512

    def _sect_size(self, offset, count):
        size = self._offset_in_sect(offset) + count
        return ((size // 512) + 1) * 512

    def read(self, count):
        sect_begin = self._sect_begin(self._offset)
        offset_in_sect = self._offset_in_sect(self._offset)
        sect_size = self._sect_size(self._offset, count)

        self._buf = self._client.read(sect_begin, sect_size)

        data = self._buf[offset_in_sect: offset_in_sect + count]

        self._offset += count

        return data

    def write(self, data):
        count = len(data)
        sect_begin = self._sect_begin(self._offset)
        offset_in_sect = self._offset_in_sect(self._offset)
        sect_size = self._sect_size(self._offset, count)

        self._buf = self._client.read(sect_begin, sect_size)

        buf = self._buf[0: offset_in_sect] + data + \
              self._buf[offset_in_sect + count:]

        self._client.write(buf, sect_begin)
        self._client.flush()

        self._offset += count

        return count

    def size(self):
        self._client.size()

if cmd == 'setargs':
    img = args[0]
    args = args[1:]
    argstr = str.join(' ', args)
    with nbd_file(img) as f:
        f.seek(args_offset)
        write_cstr(f, argstr)
elif cmd == 'getargs':
    img = args[0]
    with nbd_file(img) as f:
        f.seek(args_offset)
        print(read_cstr(f))
elif cmd == 'setsize':
    img = args[0]
    size = int(args[1])
    block_size = 32 * 1024
    blocks = (size + block_size - 1) // block_size
    f = nbd_file(img)
    f.seek(0x10)
    f.write(struct.pack('H', blocks))
    f.close()
elif cmd == 'setpartition':
    img = args[0]
    partition = int(args[1])
    start = int(args[2])
    size = int(args[3])
    partition = 0x1be + ((partition - 1) * 0x10)
    f = nbd_file(img)

    fsize = f.size()

    cyl, head, sec = chs(start // 512);
    cyl_end, head_end, sec_end = chs((start + size) // 512);

    f.seek(partition + 1)
    f.write(struct.pack('B', head))
    f.seek(partition + 5)
    f.write(struct.pack('B', head_end))

    f.seek(partition + 2)
    f.write(struct.pack('H', (cyl << 6) | sec))
    f.seek(partition + 6)
    f.write(struct.pack('H', (cyl_end << 6) | sec_end))

    system_id = 0x83
    f.seek(partition + 4)
    f.write(struct.pack('B', system_id))

    f.seek(partition + 8)
    f.write(struct.pack('I', start // 512))
    f.seek(partition + 12)
    f.write(struct.pack('I', size // 512))
    f.close()
