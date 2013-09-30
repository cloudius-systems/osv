#!/usr/bin/python

import sys, os, optparse, struct

cmd = sys.argv[1]
args = sys.argv[2:]

args_offset = 512

def chs(x):
    sec_per_track = 63
    heads = 255 

    c = (x / sec_per_track) / heads
    h = (x / sec_per_track) % heads
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
    file.write(str)
    file.write('\0')

if cmd == 'setargs':
    img = args[0]
    args = args[1:]
    argstr = str.join(' ', args)
    with file(img, 'r+b') as f:
        f.seek(args_offset)
        write_cstr(f, argstr)
elif cmd == 'getargs':
    img = args[0]
    with file(img, 'r+b') as f:
        f.seek(args_offset)
        print read_cstr(f)
elif cmd == 'setsize':
    img = args[0]
    size = int(args[1])
    block_size = 32 * 1024
    blocks = (size + block_size - 1) / block_size
    f = file(img, 'r+')
    f.seek(0x10)
    f.write(struct.pack('H', blocks))
    f.close()
elif cmd == 'setpartition':
    img = args[0]
    partition = int(args[1])
    start = int(args[2])
    size = int(args[3])
    partition = 0x1be + ((partition - 1) * 0x10)
    f = file(img, 'r+')

    f.seek(0,2)
    fsize = f.tell()

    cyl, head, sec = chs(start / 512);
    cyl_end, head_end, sec_end = chs((start + size) / 512);

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
    f.write(struct.pack('I', start / 512))
    f.seek(partition + 12)
    f.write(struct.pack('I', size / 512))
    f.close()
