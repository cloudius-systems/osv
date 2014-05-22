# OSv

OSv is a new open-source operating system for virtual-machines.
OSv was designed from the ground up to execute a single application on top
of a hypervisor, resulting in superior performance and effortless management
when compared to traditional operating systems which were designed for
a vast range of physical machines.

OSv has new APIs for new applications, but also runs unmodified Linux
applications (most of Linux's ABI is supported) and in particular can run
an unmodified JVM, and applications built on top of one.

For more information about OSv, see http://osv.io/ and
https://github.com/cloudius-systems/osv/wiki

## Documentation

* [Debugging OSv](https://github.com/cloudius-systems/osv/wiki/Debugging-OSv)
* [Trace Analysis](https://github.com/cloudius-systems/osv/wiki/Trace-analysis-using-trace.py)

## Building

OSv can only be built on a 64-bit x86 Linux distribution. Please note that
this means the "x86_64" or "amd64" version, not the 32-bit "i386" version.

First, install prerequisite packages:

**Fedora**

```
yum install ant autoconf automake boost-static gcc-c++ genromfs libvirt libtool flex bison qemu-system-x86 qemu-img maven maven-shade-plugin python-dpkt tcpdump gdb
```

**Debian stable(wheezy)**
Debian stable(wheezy) requires to compile gcc, gdb and qemu.
And also need to configure bridge manually.

More details are available on wiki page:
[Building OSv on Debian stable][]

[Building OSv on Debian stable]: https://github.com/cloudius-systems/osv/wiki/Building-OSv-on-Debian-stable

**Debian testing(jessie)**
```
apt-get install build-essential libboost-all-dev genromfs autoconf libtool openjdk-7-jdk ant qemu-utils maven libmaven-shade-plugin-java python-dpkt tcpdump gdb qemu-system-x86
```

Before start building OSv, you'll need to add your account to kvm group.
```
usermod -aG kvm <user name>
```

**Ubuntu users**: you may use [Oracle JDK][] if you don't want to pull too many
dependencies for ``openjdk-7-jdk``

[Oracle JDK]: https://launchpad.net/~webupd8team/+archive/java

To ensure functional C++11 support, Gcc 4.8 or above is required, as this was
the first version to fully comply with the C++11 standard.

Make sure all git submodules are up-to-date:

```
git submodule update --init --recursive
```

Finally, build everything at once:

```
make
```

By default make creates image in qcow2 format. To change this pass format value via img_format variable, i.e.

```
make img_format=raw
```

By default make will use the static libraries of gcc in external submodule. To change this pass `host` via *_env variables:

```
make build_env=host
```

This will use static libraries in the system instead (make sure they are installed before run `make`),
if you only want to use C++ static libraries in the system, just set `cxx_lib_env` to `host`:

```
make cxx_lib_env=host
```

## Running OSv

```
./scripts/run.py
```

By default, this runs OSv under KVM, with 4 VCPUs and 2GB of memory,
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
test invoke TCPExternalCommunication
```

## Running Java or C applications that already reside within the image:

```
# Building and running a simple java application example
$ make image=java-example
$ scripts/run.py -e "java.so -cp /java-example Hello"

# Running an ifconfig by explicit execution of ifconfig.so (compiled C++ code)
$ make
$ sudo scripts/run.py -nv -e "/tools/ifconfig.so"
```
