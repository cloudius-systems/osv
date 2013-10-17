OSv -  the best OS for cloud workloads!
=======================================

1. **install prerequisite packages:**

   On Fedora (and other RPM-based systems):
   `yum install ant autoconf automake bison boost-static flex gcc-c++ genromfs libtool libvirt`

   On Debian (and other DEB-based systems):
   `apt-get install ant autoconf build-essential genromfs libboost-all-dev libtool openjdk-7-jdk`

 > (Ubuntu users: you may use Oracle JDK in <https://launchpad.net/~webupd8team/+archive/java>
 > if you don't want to pull too many dependencies for `openjdk-7-jdk`)

 ===

 > To ensure functional `C++11` support, `gcc 4.7` or above is required.
 > `gcc 4.8` or above is recommended, as this was the first version to fully
 > comply with the `C++11` standard.


2. **make sure all git submodules are up-to-date:**

   `git submodule update --init`

3. **build everything at once:**

   `make external all`
   
   or, for faster builds (`nproc` finds the number of online `procs`):
   
   `make external all -j $(nproc)`

----------

***OR follow steps below:***
----------------------------

 - **build the specially patched libunwind:**

```bash

    cd external/libunwind
    autoreconf -i
    sh config.sh
    make
    cp ./src/.libs/libunwind.a ../..
    cd ../..
```

 - **build the glibc test suite:**

```bash
   cd external/glibc-testsuite
   make
   cd ../..
```

 - **(finally) build osv:**

   `make`
   
   and as above, for faster builds:
   
   `make -j $(nproc)`

---

To run OSv
----------

   `./scripts/run.py`

   By default, this runs OSv under KVM, with 4 VCPUs and 1GB of memory,
   and runs the default management application (containing a shell, Web
   server, and SSH server).

   If running under KVM you can terminate by hitting Ctrl+A X.

External Networking
-------------------

   To start OSv with external networking:

   `sudo ./scripts/run.py -n -v`

   The `-v` is for `KVM`'s `vhost` that provides better performance
   and it's setup requires a tap and thus we use `sudo`.

   By default OSv spawns a `dhcpd` that auto config the virtual nics.
   Static config can be done within OSv, configure networking like so:

```bash

   ifconfig virtio-net0 192.168.122.100 netmask 255.255.255.0 up
   route add default gw 192.168.122.1
```

   Test networking:

   `test TCPExternalCommunication`

   Running Java or C applications that already reside within the image:

```bash

   # The default Java-based shell and web server
   sudo scripts/run.py -nv -m4G -e "java.so -jar /usr/mgmt/web-1.0.jar app prod"

   # One of the unit tests (compiled C++ code)
   sudo scripts/run.py -nv -m4G -e "/tests/tst-pipe.so"
```


Debugging
=========

   To build with debugging symbols, and preemption off (to not confuse gdb):

   `make -j mode=debug conf-preempt=0`

   To clean debugging build's results, use:

   `make clean mode=debug`

   To run the debugging build:

   `./scripts/run.py -d`

   To connect a debugger to this run:

```bash

   gdb build/debug/loader.elf
   (gdb) connect
   (gdb) osv syms
   (gdb) bt
```

   To put a breakpoint early in the OSv run, a useful trick is tell the `VM` to
   reboot after setting the breakpoint:

```bash

   (gdb) hbreak function_name
   (gdb) monitor system_reset
   (gdb) c
```

Tracing
-------

   To add a static tracepoint, use the following code:

```C

   tracepoint<u64, int> trace_foo("foo", "x=%x y=%d");
   
   ...
   
   
   void foo(u64 x, int y)
   {
       trace_foo(x, y);
       ...
   }
```

 
|Where		|Definition							|
|---------------|---------------------------------------------------------------|
|`trace_fo`	|an internal identifier						|
|`"foo"`	|a name for the tracepoint as will be visible in the log	|
|`<u64, int>`	|parameters for the tracepoint					|
|`"x=%x y=%d"`	|format string for the tracepoint; size modifiers unneeded	|



   To enable tracepoints at runtime, use the `--trace=` switch:
 
   `scripts/imgedit.py setargs  build/release/loader.img --trace=sched\* testrunner.so`

   you can use multiple `--trace=` switches, or a single one with commas.  Shell-style wildcards
   allow enabling multiple tracepoints (as in the example). 

   If you add the `--trace-backtrace` switch, every tracepoint hit will also record
   a stack backtrace.

   To trace all function entries/returns in the program, build with `conf-tracing=1` (clean build
   needed), and enable `"function*"` tracepoints, with `--trace=`.

   To view a trace, connect with `gdb`, and:

```bash 

   (gdb) osv syms
   (gdb) set pagination off
   (gdb) set logging on
   (gdb) osv trace
```

   `gdb.txt` will contain the the trace.


Leak Detection
--------------

   Memory allocation tracking can be enabled/disabled with the `gdb` commands
   `"osv leak on"`, `"osv leak off"`, but even easier is to add the `"--leak"`
   paramter to the loader, to have it start leak detection when entering the
   payload (not during OSv boot). For example:

```bash
	scripts/run.py -e "--leak java.so -jar cli.jar"

	scripts/run.py -e "--leak tests/tst-leak.so"
```

   Leak detection can be used in either the debug or release build, though
   note that the release builds' optimizations may somewhat modify the
   reported call traces.

   When the run ends, connect to OSv with the debugger to see the where
   the remaining allocation come from:

```bash

   gdb build/release/loader.elf       # or build/debug/loader.elf
   (gdb) connect
   (gdb) osv syms
   (gdb) set logging on	# optional, if you want to save the output to a file
   (gdb) set height 0	# optional, useful if using "set logging on"
   (gdb) osv leak show
```

   Please note that `"osv leak show"` can take a L-O-N-G time - even a few
   minutes, especially when running Java, as it allocates myriads of objects
   that `gdb` will now have to inspect.


Use-after-free and overrun detection
------------------------------------

   Set `conf-debug_memory=1` in `base.mak`, and perform a clean build.  A use-after-free will result
   in an immediate segmentation fault.

   Note that the detector requires a lot of memory, so you may need to start a larger guest.

Running Java benchmarks
-----------------------

   After running `make`, do

```bash

   ./scripts/run.py -e "java.so -jar /tests/bench/bench.jar"
```

Profiling
---------

   You can use 'perf kvm' for profiling:

```bash

   perf kvm --guestvmlinux=build/release/loader.elf top
```
