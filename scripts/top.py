#!/usr/bin/env python3
# Connect to a running OSv guest through the HTTP API and periodically display
# the list of threads getting most CPU time, similarly to the Linux top(1)
# command.
#
# The host:port to connect to can be specified on the command line, or
# defaults to localhost:8000 (which is where "run.py --api" redirects the
# guest's API port). If only a hostname is specified, port 8000 is used by
# default.

import requests
import argparse
import json
import time
import sys
import collections

from osv.client import Client

try:
    import curses
    curses.setupterm()
    clear = curses.tigetstr('clear').decode()
except:
    clear = '\033[H\033[2J'

parser = argparse.ArgumentParser(description="""
    Connects to a running OSv guest through the HTTP API and periodically displays
    the list of threads getting most CPU time, similarly to the Linux top(1)
    command.""")
Client.add_arguments(parser)
parser.add_argument('-s','--switches', help='show context-switch information', action="store_true")
parser.add_argument('-l','--lines', help='number of top threads to show', type=int, default=20)
parser.add_argument('-i','--idle', help='show idle threads as normal threads', action="store_true")
parser.add_argument('-p','--period', help='refresh period (in seconds)', type=float, default=2.0)

args = parser.parse_args()
client = Client(args)

url = client.get_url() + "/os/threads"
ssl_kwargs = client.get_request_kwargs()

# Definition of all possible columns that top.py supports - and how to
# calculate them. We'll later pick which columns we really want to show.
columns = [
  {
    'name': 'ID',
    'width': 5,
    'source': 'id',
  },
  {
    'name': 'CPU',
    'width': 3,
    'source': 'cpu',
  },
  {
    'name': '%CPU',
    'width': 5,
    'format': '%5.1f',
    'source': 'cpu_ms',
    'rate': True,
    'multiplier': 0.1,
  },
  {
    'name': 'TIME',
    'width': 7,
    'format': '%7.2f',
    'source': 'cpu_ms',
    'multiplier': 0.001,
  },
  {
    'name': 'NAME',
    'source': 'name',
  },
  {
    'name': 'sw',
    'width': '7',
    'source': 'switches',
  },
  {
    'name': 'sw/s',
    'width': '7',
    'format': '%7.1f',
    'source': 'switches',
    'rate': True,
  },
  {
    'name': 'us/sw',
    'width': '5',
    'format': '%5.0f',
    'source': 'cpu_ms',
    'multiplier': 1000.0,
    'rateby': 'switches'
  },
  {
    'name': 'preempt',
    'width': '7',
    'source': 'preemptions',
  },
  {
    'name': 'pre/s',
    'width': '7',
    'format': '%7.1f',
    'source': 'preemptions',
    'rate': True,
  },
  {
    'name': 'mig',
    'width': '6',
    'source': 'migrations',
  },
  {
    'name': 'mig/s',
    'width': '5',
    'format': '%5.1f',
    'source': 'migrations',
    'rate': True,
  },
]

# Choose, according to command line parameters, which columns we really
# want to show. In the future we can easily extend this to make the choice
# more flexible.
cols = ['ID', 'CPU', '%CPU', 'TIME']
if args.switches:
    cols += ['sw', 'sw/s', 'us/sw', 'preempt', 'pre/s', 'mig', 'mig/s']
cols += ['NAME']


# Extract from "columns" only the columns requested by "cols", in that order
requested_columns = [next(col for col in columns if col['name'] == name) for name in cols]

prev = dict();
previdles = dict();
timems = 0
while True:
    start_refresh = time.time()
    result = requests.get(url, **ssl_kwargs).json()
    print(clear, end='')
    newtimems = result['time_ms']

    print("%d threads " % (len(result['list'])), end='')

    cur = collections.defaultdict(dict);
    idles = dict()

    for thread in result['list']:
        id = thread['id']
        for col in requested_columns:
            cur[id][col['source']] = thread[col['source']]

        # Some silly fixes
        if cur[id]['cpu'] == 0xffffffff:
            # This thread was never run on any CPU
            cur[id]['cpu'] = '-'

        # If -i (--idle) option is not given, remove idle threads from the
        # normal list, and instead display just idle percentage:
        if not args.idle and thread['name'].startswith('idle'):
            idles[thread['name']] = cur[id]['cpu_ms']
            del cur[id]

    if idles:
        if timems:
            print("on %d CPUs; idle: " % (len(idles)), end='')
            total = 0.0
            for n in sorted(idles):
                percent = 100.0*(idles[n]-previdles[n])/(newtimems - timems)
                total += percent
                print ("%3.0f%% "%percent, end='')
            if len(idles) > 1:
                print(" (total %3.0f%%)" % total, end='')
        previdles = idles
    print()

    # Print title line
    for col in requested_columns:
        if 'width' in col:
            print(("%"+str(col['width'])+"s ") % col['name'], end='')
        else:
            print(col['name']+" ", end='')
    print()

    if timems:
        # We now have for all threads the current and previous measurements,
        # so can can sort the threads and calculate the data to display
        for id in sorted(cur, key=lambda x : (cur[x]['cpu_ms']-prev[x].get('cpu_ms',0), prev[x].get('cpu_ms',0)), reverse=True)[:args.lines]:
            if not 'cpu_ms' in prev[id]:
                # A new thread.
                continue
            for col in requested_columns:
                val = cur[id][col['source']]
                if 'rate' in col and col['rate']:
                    val -= prev[id][col['source']]
                    val /= (newtimems - timems) / 1000.0
                elif 'rateby' in col:
                    val -= prev[id][col['source']]
                    if val:
                        val /= cur[id][col['rateby']] - prev[id][col['rateby']]

                if 'multiplier' in col:
                    val *= col['multiplier']

                if 'format' in col:
                    format = col['format']+" "
                elif 'width' in col:
                    format = "%" + str(col['width']) + "s "
                else:
                    format = "%s "
                print(format % val, end='')
            print()

    timems = newtimems
    prev = cur
    time.sleep(max(0, args.period - (time.time() - start_refresh)))
