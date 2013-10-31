#!/usr/bin/python2

# ant insists of being not very quiet, with output like
#
# BUILD SUCCESSFUL
# Total time: 0 seconds
#
# This script runs ant but detects this output and trims it.

import os, sys, subprocess

try:
    ant = subprocess.Popen(sys.argv[1:],
                           stdout = subprocess.PIPE,
                           stderr = subprocess.PIPE)
except OSError:
     print "Apache Ant not found. Please install ant package."
     sys.exit(1)

out = ant.communicate()[0]

lines = out.splitlines()
if not (len(lines) == 3 and lines[0] == '' and lines[1] == 'BUILD SUCCESSFUL'):
    print out,

sys.exit(ant.returncode)
