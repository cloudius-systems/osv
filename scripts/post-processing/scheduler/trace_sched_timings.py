import re
import os
import sys

l = file("gdb.txt", "rt").readlines()

# thread->[(time, duration), ...]
threads = {}

# thread -> time of schedule-in
sched = {}

for tr in l:
    (threadp, cpu, sec, microsec, event, info) = re.match("(0x.*?)\s+([0-9]+)\s+([0-9]+)\.([0-9]+)\s+(\S+)\s+(.*)\n", tr).groups()
    sec = int(sec)
    microsec = int(microsec)

    if (event == "sched_switch"):
        fromm = threadp
        if (sched.has_key(fromm)):
            if (not threads.has_key(fromm)):
                threads[fromm] = []

            threads[fromm] += [(sec - sched[fromm][0], microsec - sched[fromm][1])]

            del sched[fromm]

        to = re.match("to (.*)", info).group(1)
        assert (not sched.has_key(to))
        sched[to] = (sec, microsec)

for tr in threads:
    f = file("thread_%s_timings.txt" % tr, "wt")

    for timing in threads[tr]:
        f.write("%d\n" % (timing[0]*1000000 + timing[1]))
    
    f.close()

