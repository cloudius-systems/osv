#!/usr/bin/python

import sys, os, optparse, struct

cmd = sys.argv[1]
args = sys.argv[2:]

if cmd == 'setargs':
    img = args[0]
    args = args[1:]
    argstr = str.join(' ', args) + '\0'
    with file(img, 'r+b') as f:
        f.seek(512)
        f.write(argstr)
elif cmd == 'setsize':
    img = args[0]
    size = int(args[1])
    block_size = 32 * 1024
    blocks = (size + block_size - 1) / block_size
    f = file(img, 'r+')
    f.seek(512-4)
    f.write(struct.pack('H', blocks))
    f.close()
