Thread time-slice histogram
===========================

Use this tool to investigate the time-slices of threads in OSv.

HOW-TO:

1. Start OSv with --trace=sched_switch

2. Run your test

3. Connect with gdb and perform:
    (gdb) connect
    (gdb) logtrace  // this will dump the sched_switch tracepoints to gdb.txt

4. Run trace_sched_timings.py, it will analyze gdb.txt and write a new text file for each thread
   that was scheduled during the tracepoint snapshot, the new files will contain histogram of 
   time slices.

5. Run histo.py for to graph the results visually



