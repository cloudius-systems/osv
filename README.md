# OSv

add by jackson

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

If you wish, you can run the script 'scripts/setup.py' as root to install all dependencies.
Otherwise, you may follow the manual instructions below.

First, install prerequisite packages:

**Fedora**

```
scripts/setup.py
```

**Debian stable(wheezy)**
Debian stable(wheezy) requires to compile gcc, gdb and qemu.
And also need to configure bridge manually.

More details are available on wiki page:
[Building OSv on Debian stable][]

[Building OSv on Debian stable]: https://github.com/cloudius-systems/osv/wiki/Building-OSv-on-Debian-stable

**Debian testing(jessie)**
```
apt-get install build-essential libboost-all-dev genromfs autoconf libtool openjdk-7-jdk ant qemu-utils maven libmaven-shade-plugin-java python-dpkt tcpdump gdb qemu-system-x86 gawk gnutls-bin openssl python-requests lib32stdc++-4.9-dev p11-kit
```

**Arch Linux**
```
pacman -S base-devel git python apache-ant maven qemu gdb boost yaml-cpp unzip openssl-1.0
```

Apply the following patch to make it work with openssl-1.0 
```
diff --git a/modules/lua/Makefile b/modules/lua/Makefile
index 9676f349..ddb6a075 100644
--- a/modules/lua/Makefile
+++ b/modules/lua/Makefile
@@ -123,7 +123,7 @@ $(CDIR)/ssl.lua: $(LUA_ROCKS_BIN)
 
 # Workaround because LuaRocks ignores /lib64
 ifneq ("$(wildcard /usr/lib64/libssl.so*)", "")
-       out/bin/luarocks install LuaSec 0.5 OPENSSL_LIBDIR=/usr/lib64
+       out/bin/luarocks install LuaSec 0.5 OPENSSL_LIBDIR=/usr/lib/openssl-1.0 OPENSSL_INCDIR=/usr/include/openssl-1.0
 else
        out/bin/luarocks install LuaSec 0.5
 endif
```

Before start building OSv, you'll need to add your account to kvm group.
```
usermod -aG kvm <user name>
```

**Ubuntu**:

```
scripts/setup.py
```

You may use [Oracle JDK][] if you don't want to pull too many
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

to build only the OSv kernel, or more usefully,

```
scripts/build
```

to build an image of the OSv kernel and the default application.

scripts/build creates the image ```build/last/usr.img``` in qcow2 format.
To convert this image to other formats, use the ```scripts/convert```
tool, which can create an image in the vmdk, vdi, raw, or qcow2-old formats
(qcow2-old is an older qcow2 format, compatible with older versions of QEMU).
For example:

```
scripts/convert raw
```

By default make will use the static libraries and headers of gcc in external submodule. To change this pass `host` via *_env variables:

```
make build_env=host
```

This will use static libraries and headers in the system instead (make sure they are installed before run `make`),
if you only want to use C++ static libraries in the system, just set `cxx_lib_env` to `host`:

```
make cxx_lib_env=host
```

## Running OSv

```
./scripts/run.py
```

By default, this runs OSv under KVM, with 4 VCPUs and 2GB of memory,
and runs the default management application containing a shell, a
REST API server and a browser base dashboard (at port 8000).

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
$ scripts/build image=java-example
$ scripts/run.py -e "java.so -cp /java-example Hello"

# Running an ifconfig by explicit execution of ifconfig.so (compiled C++ code)
$ scripts/build
$ sudo scripts/run.py -nv -e "/tools/ifconfig.so"
```
