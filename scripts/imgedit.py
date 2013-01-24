#!/usr/bin/python

import sys, os, optparse

cmd = sys.argv[1]
args = sys.argv[2:]

if cmd == 'setargs':
    img = args[0]
    args = args[1:]
    argstr = str.join(' ', args) + '\0'
    with file(img, 'r+b') as f:
        f.seek(512)
        f.write(argstr)
