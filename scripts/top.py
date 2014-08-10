#!/usr/bin/env python3
# Connect to a running OSv guest through the HTTP API and periodically display
# the list of threads getting most CPU time, similarly to the Linux top(1)
# command.
#
# The host:port to connect to can be specified on the command line, or
# defaults to localhost:8000 (which is where "run.py --api" redirects the
# guest's API port). If only a hostname is specified, port 8000 is used by
# default.

import urllib.request
import json
import time
import sys
import collections

try:
    import curses
    curses.setupterm()
    clear = curses.tigetstr('clear').decode()
except:
    clear = '\033[H\033[2J'

hostport = 'localhost:8000'
if len(sys.argv) > 1:
    if ':' in sys.argv[1]:
        hostport = sys.argv[1]
    else:
        hostport = sys.argv[1] + ":8000"

period = 2.0  # How many seconds between refreshes

prevtime = collections.defaultdict(int)
name = dict()
timems = 0
while True:
    start_refresh = time.time()
    result_json = urllib.request.urlopen("http://" + hostport + "/os/threads").read().decode()
    result = json.loads(result_json)
    print(clear, end='')
    newtimems = result['time_ms']

    print("%d threads " % (len(result['list'])), end='')
    diff = dict()
    idles = dict()
    for thread in result['list']:
        id = thread['id']
        cpums = thread['cpu_ms']
        if timems:
            diff[id] = cpums - prevtime[id]
        prevtime[id] = cpums
        name[id] = thread['name']
        # Display per-cpu idle threads differently from normal threads
        if thread['name'].startswith('idle') and timems:
            idles[thread['name']] = diff[id]
            del diff[id]

    if idles:
        print("on %d CPUs; idle: " % (len(idles)), end='')
        total = 0.0
        for n in sorted(idles):
            percent = 100.0*idles[n]/(newtimems - timems)
            total += percent
            print ("%3.0f%% "%percent, end='')
        if len(idles) > 1:
            print(" (total %3.0f%%)" % total)
    print("")

    print("%5s  %s %7s %s" % ("ID", "%CPU", "TIME", "NAME"))
    for id in sorted(diff, key=lambda x : (diff[x], prevtime[x]), reverse=True)[:20]:
        percent = 100.0*diff[id]/(newtimems - timems)
        print("%5d %5.1f %7.2f %s" % (id, percent, prevtime[id]/1000.0, name[id]))
    timems = newtimems
    time.sleep(max(0, period - (time.time() - start_refresh)))
