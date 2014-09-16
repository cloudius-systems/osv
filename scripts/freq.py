#!/usr/bin/env python3
# Connect to a running OSv guest through the HTTP API and periodically
# display the the frequency of chosen tracepoints.
#
# Usage: freq.py host[:port] [tracepoint] ...
#
# If the list of tracepoints whose frequencies to display is missing,
# freq.py will display, together with the "Usage:" message, also
# the list of all available tracepoints.

import urllib.request
import json
import time
import sys
import collections
import signal

hostport = 'localhost:8000'
if len(sys.argv) > 1:
    if ':' in sys.argv[1]:
        hostport = sys.argv[1]
    else:
        hostport = sys.argv[1] + ":8000"

if len(sys.argv) <= 2:
    print("Usage: %s host[:port] [tracepoint] ...\n" % sys.argv[0])
    print("Valid values for <tracepoint>:\n")
    # Display list of valid tracepoints
    result_json = urllib.request.urlopen("http://" + hostport + "/trace/status").read().decode()
    result = json.loads(result_json)
    tracepoints = set()
    for tracepoint in result:
        tracepoints.add(tracepoint['name'])
    for tracepoint in sorted(tracepoints):
        print(tracepoint)
    sys.exit()


tracepoint_names= sys.argv[2:]

period = 2.0  # How many seconds between refreshes

print("Showing number of events per second. Refreshing every %g seconds" % period)

def make_request(url, data, method):
    opener = urllib.request.build_opener(urllib.request.HTTPHandler)
    request = urllib.request.Request(url, data)
    request.get_method = lambda: method
    url = opener.open(request)
    return url.read().decode()

def delete_all_counters():
    make_request('http://' + hostport + '/trace/count', None, 'DELETE')

def enable_counter(name):
    make_request('http://' + hostport + '/trace/count/' + name, 'enabled=true'.encode(), 'POST')

delete_all_counters()

def signal_handler(signal, frame):
    delete_all_counters()
    sys.exit(0)
signal.signal(signal.SIGINT, signal_handler)
signal.signal(signal.SIGTERM, signal_handler)
signal.signal(signal.SIGHUP, signal_handler)

for t in tracepoint_names:
    try:
        enable_counter(t)
    except:
        print("Unrecognized tracepoint %s" % t)
        delete_all_counters()
        sys.exit()

def get_counts():
    result_json = make_request('http://' + hostport + '/trace/count', None, 'GET')
    return json.loads(result_json)
prevcount = collections.defaultdict()

widths = [max(15, len(t)) for t in tracepoint_names]
header = ' '.join('%%%ds' % w for w in widths) % tuple(tracepoint_names)
format_string = ' '.join('%%%d.0f' % w for w in widths)

nline = 0
timems = 0
while True:
    if (nline % 20) == 0:
        print(header)
    nline = nline + 1

    start_refresh = time.time()
    result = get_counts()
    newtimems = result['time_ms']
    freq = dict()
    for i in result['list']:
        name = i['name']
        count = i['count']
        if timems > 0:
            freq[name] = (count - prevcount[name]) / ((newtimems - timems) / 1000.0)
        prevcount[name] = count
    if timems > 0:
        print(format_string % tuple(map(lambda t: freq[t], tracepoint_names)))
    time.sleep(max(0, period - (time.time() - start_refresh)))
    timems = newtimems


delete_all_counters()
