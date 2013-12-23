# OSv

## Building

First, install prerequisite packages:

**Fedora**

```
yum install ant autoconf automake boost-static gcc-c++ genromfs libvirt libtool flex bison qemu-system-x86 qemu-img
```

**Debian**

```
apt-get install build-essential libboost-all-dev genromfs autoconf libtool openjdk-7-jdk ant
```

**Ubuntu users**: you may use [Oracle JDK][] if you don't want to pull too many
dependencies for ``openjdk-7-jdk``

[Oracle JDK]: https://launchpad.net/~webupd8team/+archive/java

To ensure functional C++11 support, Gcc 4.7 or above is required.  Gcc 4.8 or
above is recommended, as this was the first version to fully comply with the
C++11 standard.

Make sure all git submodules are up-to-date:

```
git submodule update --init
```

Finally, build everything at once:

```
make external all 
```

By default make creates image in qcow2 format. To change this pass format value via img_format variable, i.e.

```
make img_format=raw external all
```

## Running OSv

```
./scripts/run.py
```

By default, this runs OSv under KVM, with 4 VCPUs and 1GB of memory,
and runs the default management application (containing a shell, Web
server, and SSH server).

If running under KVM you can terminate by hitting Ctrl+A X.


## External Networking

To start osv with external networking:

```
sudo ./scripts/run.py -n -v
```

The -v is for kvm's vhost that provides better performance
and its setup requires a tap and thus we use sudo.

By default OSv spawns a dhcpd that auto config the virtual nics.
Static config can be done within OSv, configure networking like so:

```
ifconfig virtio-net0 192.168.122.100 netmask 255.255.255.0 up
route add default gw 192.168.122.1
```

Test networking:

```
test TCPExternalCommunication
```

Running Java or C applications that already reside within the image:

```
# The default Java-based shell and web server
sudo scripts/run.py -nv -m4G -e "java.so -jar /usr/mgmt/web-1.0.0.jar app prod"

# One of the unit tests (compiled C++ code)
$ sudo scripts/run.py -nv -m4G -e "/tests/tst-pipe.so"
```

## Debugging

For more information about debugging OSv and applications on OSv, please
refer to https://github.com/cloudius-systems/osv/wiki/Debugging-OSv

To attach a debugger to a running OSv guest, run:

```
$ gdb build/release/loader.elf
(gdb) connect
(gdb) osv syms
(gdb) bt
```

To put a breakpoint early in the osv run, a useful trick is tell the vm to
reboot after setting the breakpoint:

```
(gdb) hbreak function_name
(gdb) monitor system_reset
(gdb) c
```

To compile OSv with optimizations disabled (this may be useful for debugging
when you see an important variable or function optimized out), run

```
make mode=debug
```

To run that mode=debug build and debug it, use:

```
./scripts/run.py -d
gdb build/debug/loader.elf
```

To clean the mode=debug build,

```
make mode=debug clean
```

## Tracing

To add a static tracepoint, use the following code:

```c++ 
tracepoint<u64, int> trace_foo("foo", "x=%x y=%d");

void foo(u64 x, int y)
{
    trace_foo(x, y);

    // ...
}
```
 
where:

``` 
trace_foo: an internal identifier
"foo": a name for the tracepoint as will be visible in the log
<u64, int>: parameters for the tracepoint
"x=%x y=%d": format string for the tracepoint; size modifiers unneeded
``` 
 
To enable tracepoints at runtime, use the --trace= switch:

``` 
scripts/imgedit.py setargs  build/release/loader.img --trace=sched\* testrunner.so
```
 
you can use multiple --trace= switches, or a single one with commas.
Shell-style wildcards allow enabling multiple tracepoints (as in the example). 
 
If you add the --trace-backtrace switch, every tracepoint hit will also record
a stack backtrace.

To trace all function entries/returns in the program, build with conf-tracing=1
(clean build needed), and enable "function\*" tracepoints, with --trace=.
 
To view a trace, connect with gdb, and:

``` 
(gdb) osv syms
(gdb) set pagination off
(gdb) set logging on
(gdb) osv trace
```

gdb.txt will contain the the trace.

## Leak Detection

Memory allocation tracking can be enabled/disabled with the gdb commands
"osv leak on", "osv leak off", but even easier is to add the "--leak"
parameter to the loader, to have it start leak detection when entering the
payload (not during OSv boot). For example:

```
scripts/run.py -e "--leak java.so -jar cli.jar"

scripts/run.py -e "--leak tests/tst-leak.so"
```

Leak detection can be used in either the debug or release build, though
note that the release builds' optimizations may somewhat modify the
reported call traces.

When the run ends, connect to OSV with the debugger to see the where
the remaining allocation come from:

```
$ gdb build/release/loader.elf       # or build/debug/loader.elf
(gdb) connect
(gdb) osv syms
(gdb) set logging on	# optional, if you want to save the output to a file
(gdb) set height 0	# optional, useful if using "set logging on"
(gdb) osv leak show
```

Please note that "osv leak show" can take a L-O-N-G time - even a few
minutes, especially when running Java, as it allocates myriads of objects
that GDB will now have to inspect.


## Use-after-free and overrun detection

Set conf-debug_memory=1 in base.mak, and perform a clean build.  A use-after-free will result
in an immediate segmentation fault.

Note that the detector requires a lot of memory, so you may need to start a larger guest.

## Running Java benchmarks

After running "make", do

```
./scripts/run.py -e "java.so -jar /tests/bench/bench.jar"
```

## Profiling

You can use ``perf kvm`` for profiling:

```
perf kvm --guestvmlinux=build/release/loader.elf top
```
