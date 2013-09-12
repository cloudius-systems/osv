Memory Analyzer

HOW-TO:

1. Run OSv with --trace=memory_* to dump all malloc/free related tracepoints

2. Run your test / workload

3. Connect with gdb and execute:
    (gdb) connect
    (gdb) osv trace2file // will dump the tracepoints to trace.txt

4. Run the memory_analyzer script and it will show you what happened 
   during the snapshot (unfreed buffers and sizes histograms)

TIP of the day (for hardcore developers):

If the tracepoint buffer is not sufficient to capture all the desired tracepoints
it is possible to increase it's size statically by modifying trace.cc




