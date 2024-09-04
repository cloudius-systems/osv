***OSv was originally designed and implemented by Cloudius Systems (now ScyllaDB) however
 currently, it is being maintained and enhanced by a small community of volunteers.
 If you are into systems programming or want to learn and help us improve OSv, then please
 contact us on [OSv Google Group forum](https://groups.google.com/forum/#!forum/osv-dev)
 or feel free to pick up any [good issues for newcomers](https://github.com/cloudius-systems/osv/labels/good-for-newcomers).
 For details on how to format and send patches, please read
 [this wiki](https://github.com/cloudius-systems/osv/wiki/Formatting-and-sending-patches)
 (__we do accept pull requests as well__).***

# OSv

OSv is an open-source versatile modular **unikernel** designed to run single **unmodified
Linux application** securely as microVM on top of a hypervisor, when compared to traditional
operating systems which were designed for a vast range of physical machines. Built from
the ground up for effortless deployment and management of microservices
and serverless apps, with superior performance.

OSv has been designed to run unmodified x86-64 and aarch64 Linux
binaries **as is**, which effectively makes it a **Linux binary compatible unikernel**
(for more details about Linux ABI compatibility please read
[this doc](https://github.com/cloudius-systems/osv/wiki/OSv-Linux-ABI-Compatibility)).
In particular, OSv can run many managed language runtimes including
[**JVM**](https://github.com/cloudius-systems/osv-apps/tree/master/java-example),
[**Python**](https://github.com/cloudius-systems/osv-apps/tree/master/python-from-host),
[**Node.JS**](https://github.com/cloudius-systems/osv-apps/tree/master/node-from-host),
[**Ruby**](https://github.com/cloudius-systems/osv-apps/tree/master/ruby-example), **Erlang**,
and applications built on top of those runtimes.
It can also run applications written in languages compiling directly to native machine code like
**C**, **C++**,
[**Golang**](https://github.com/cloudius-systems/osv-apps/tree/master/golang-httpserver)
and [**Rust**](https://github.com/cloudius-systems/osv-apps/tree/master/rust-httpserver)
as well as native images produced
by [**GraalVM**](https://github.com/cloudius-systems/osv-apps/tree/master/graalvm-example)
and [WebAssembly/Wasmer](https://github.com/cloudius-systems/osv-apps/tree/master/webassembly).

OSv can boot as fast as **~5 ms** on Firecracker using as low as 11 MB of memory.
OSv can run on many hypervisors including QEMU/KVM,
[Firecracker](https://github.com/cloudius-systems/osv/wiki/Running-OSv-on-Firecracker),
[Cloud Hypervisor](https://github.com/cloudius-systems/osv/wiki/Running-OSv-on-Cloud-Hypervisor),
Xen, [VMWare](https://github.com/cloudius-systems/osv/wiki/Running-OSv-on-VMware-ESXi),
[VirtualBox](https://github.com/cloudius-systems/osv/wiki/Running-OSv-on-VirtualBox) and
Hyperkit as well as open clouds like AWS EC2, GCE and OpenStack.

For more information about OSv, see the [main wiki page](https://github.com/cloudius-systems/osv/wiki)
and http://osv.io/.

## Building and Running Apps on OSv

To run an application on OSv, one needs to build an image by fusing the OSv kernel, and
the application files together. This, at a high level, can be achieved in two ways, either:
- by using the shell script located at `./scripts/build`
 that builds the kernel from sources and fuses it with application files, or
- by using the [capstan tool](https://github.com/cloudius-systems/capstan) that uses *pre-built
 kernel* and combines it with application files to produce a final image.

If you intend to try to run your app on OSv with the least effort possible, you should pursue the *capstan*
route. For introduction please read this 
[crash course](https://github.com/cloudius-systems/osv/wiki/Build-and-run-apps-on-OSv-using-Capstan).
For more details about *capstan* please read 
this more detailed [documentation](https://github.com/cloudius-systems/capstan#documentation). Pre-built OSv kernel files
(`osv-loader.qemu`) can be automatically downloaded by *capstan* from 
the [OSv regular releases page](https://github.com/cloudius-systems/osv/releases) or manually from 
the [nightly releases repo](https://github.com/osvunikernel/osv-nightly-releases/releases/tag/ci-master-latest).

If you are comfortable with make and GCC toolchain and want to try the latest OSv code, then you should
read this [part of the readme](#setting-up-development-environment) to guide you how to set up your
 development environment and build OSv kernel and application images.

## Releases

We aim to release OSv 2-3 times a year. You can find the [latest one on github](https://github.com/cloudius-systems/osv/releases)
along with several published artifacts including kernel and some modules.

In addition, we have set up [Travis-based CI/CD pipeline](https://travis-ci.org/github/cloudius-systems/osv) where each
commit to the master and ipv6 branches triggers full build of the latest kernel and publishes some artifacts to 
the [nightly releases repo](https://github.com/osvunikernel/osv-nightly-releases/releases). Each commit also
triggers the publishing of new Docker "build toolchain" images to the [Docker hub](https://hub.docker.com/u/osvunikernel).

## Design

A good bit of the design of OSv is pretty well explained in 
the [Components of OSv](https://github.com/cloudius-systems/osv/wiki/Components-of-OSv) wiki page. You 
can find even more information in the original 
[USENIX paper and its presentation](https://www.usenix.org/conference/atc14/technical-sessions/presentation/kivity).

In addition, you can find a lot of good information about the design of specific OSv components on
the [main wiki page](https://github.com/cloudius-systems/osv/wiki) and http://osv.io/ and http://blog.osv.io/.
Unfortunately, some of that information may be outdated (especially on http://osv.io/), so it is always
best to ask on the [mailing list](https://groups.google.com/forum/#!forum/osv-dev) if in doubt.

## Component Diagram
In the diagram below, you can see the major components of OSv across the logical layers. Starting with `libc` at the top, which is greatly based on `musl`, the core layer in the middle comprises ELF dynamic linker, VFS, networking stack, thread scheduler, page cache, RCU, and memory management components. Then finally down, the layer is composed of the clock, block, and networking device drivers that allow OSv to interact with hypervisors like VMware and VirtualBox or the ones based on KVM and XEN.
![Component Diagram](../master/documentation/OSv_Component_Diagram.png)

## Metrics and Performance

There are no official **up-to date** performance metrics comparing OSv to other unikernels or Linux.
In general, OSv lags behind Linux in disk-I/O-intensive workloads partially due to coarse-grained locking 
in VFS around read/write operations as described in this [issue](https://github.com/cloudius-systems/osv/issues/450).
In network-I/O-intensive workloads, OSv should fare better (or at least used to as Linux has advanced a lot since)
as shown with performance tests of Redis and [Memcached](https://github.com/cloudius-systems/osv/wiki/OSv-Case-Study:-Memcached).
You can find some old "numbers" on the main wiki, http://osv.io/benchmarks, and some papers listed at the bottom of this readme.

So OSv is probably not best suited to run MySQL or ElasticSearch, but should deliver pretty solid performance for general
 stateless applications like microservices or serverless (at least as some papers show).

### Kernel Size

At this moment (as of December 2022) the size of the universal OSv kernel (`loader.elf` artifact) *built with all symbols hidden* is around
3.6 MB. The size of the kernel linked with the full `libstdc++.so.6` library and ZFS filesystem library included is 6.8 MB. Please read the [Modularization](https://github.com/cloudius-systems/osv/wiki/Modularization) wiki to better understand how kernel can be built and further reduced in size and customized to run on a specific hypervisor or a specific app.

The size of OSv kernel may be considered quite large compared to other unikernels. However, bear in mind that OSv kernel (being unikernel) provides **subset** of the functionality of the following Linux libraries (see their approximate size on Linux host):
- `libresolv.so.2` (_100 K_)
- `libc.so.6` (_2 MB_)
- `libm.so.6` (_1.4 MB_)
- `ld-linux-x86-64.so.2` (_184 K_)
- `libpthread.so.0` (_156 K_)
- `libdl.so.2` (_20 K_)
- `librt.so.1` (_40 K_)
- `libstdc++.so.6` (_2 MB_)
- `libaio.so.1` (_16 K_)
- `libxenstore.so.3.0` (_32 K_)
- `libcrypt.so.1` (_44 K_)

### Boot Time

OSv, with _Read-Only FS and networking off_, can boot as fast as **~5 ms** on Firecracker 
and even faster around **~3 ms** on QEMU with the microvm machine. However, in general, the boot time
will depend on many factors like hypervisor including settings of individual para-virtual devices, 
filesystem (ZFS, ROFS, RAMFS, or Virtio-FS), and some boot parameters. Please note that by default OSv images
get built with ZFS filesystem.

For example, the boot time of ZFS image on Firecracker is ~40 ms, and regular QEMU ~200 ms these days. Also,
newer versions of QEMU (>=4.0) are typically faster to boot. Booting on QEMU in PVH/HVM mode (aka direct kernel boot, enabled 
by `-k` option of `run.py`) should always be faster as OSv is directly invoked in 64-bit long mode. Please see
[this Wiki](https://github.com/cloudius-systems/osv/wiki/OSv-boot-methods-overview) for a brief review of the boot
methods OSv supports.

Finally, some boot parameters passed to the kernel may affect the boot time:
- `--console serial` - this disables VGA console that is [slow to initialize](https://github.com/cloudius-systems/osv/issues/987) and can shave off 60-70 ms on QEMU
- `--nopci` - this disables enumeration of PCI devices especially if we know none are present (QEMU with microvm or Firecracker) and can shave off 10-20 ms 
- `--redirect=/tmp/out` - writing to the console can impact the performance quite severely (30-40%) if application logs 
a lot, so redirecting standard output and error to a file might speed up performance quite a lot

You can always see boot time breakdown by adding `--bootchart` parameter:
```
./scripts/run.py -e '--bootchart /hello'
OSv v0.57.0-6-gb442a218
eth0: 192.168.122.15
	disk read (real mode): 58.62ms, (+58.62ms)
	uncompress lzloader.elf: 77.20ms, (+18.58ms)
	TLS initialization: 77.96ms, (+0.76ms)
	.init functions: 79.75ms, (+1.79ms)
	SMP launched: 80.11ms, (+0.36ms)
	VFS initialized: 81.62ms, (+1.52ms)
	Network initialized: 81.78ms, (+0.15ms)
	pvpanic done: 81.91ms, (+0.14ms)
	pci enumerated: 93.89ms, (+11.98ms)
	drivers probe: 93.89ms, (+0.00ms)
	drivers loaded: 174.80ms, (+80.91ms)
	ROFS mounted: 176.88ms, (+2.08ms)
	Total time: 178.01ms, (+1.13ms)
Cmdline: /hello
Hello from C code
```

### Memory Utilization

OSv needs at least 11 M of memory to run a _hello world_ app. Even though it is a third of what it was 4 years ago, it is still quite a lot compared to other unikernels. The applications spawning many threads may take advantage of building the kernel with the option `conf_lazy_stack=1` to further reduce memory utilization (please see the comments of this [patch](https://github.com/cloudius-systems/osv/commit/f5684d9c3f4f8d20a64605cfe66fd51771754256) to understand this feature better). 

We are planning to further lower this number by adding [self-tuning logic to L1/L2 memory pools](https://github.com/cloudius-systems/osv/issues/1013).

## Testing

OSv comes with around 140 unit tests that get executed upon every commit and run on ScyllaDB servers. There are also a number of extra
tests located under `tests/` sub-tree that are not automated at this point.

You can run unit tests in a number of ways:
```
./scripts/build check                  # Create ZFS test image and run all tests on QEMU

./scripts/build check fs=rofs          # Create ROFS test image and run all tests on QEMU

./scripts/build image=tests && \       # Create ZFS test image and run all tests on Firecracker
./scripts/test.py -p firecracker

./scripts/build image=tests && \       # Create ZFS test image and run all tests on QEMU
./scripts/test.py -p qemu_microvm      # with microvm machine
```

In addition, there is an [Automated Testing Framework](https://github.com/cloudius-systems/osv/wiki/Automated-Testing-Framework)
that can be used to run around 30 real apps, some of them
under stress using `ab` or `wrk` tools. The intention is to catch any regressions that may be missed
by unit tests.

Finally, one can use [Docker files](https://github.com/cloudius-systems/osv/tree/master/docker#docker-osv-builder) to
test OSv on different Linux distributions.

## Setting up Development Environment

OSv can only be built on a 64-bit x86 and ARM Linux distribution. Please note that
this means the "x86_64" or "amd64" version for 64-bit x86 and "aarch64" or "arm64" version for ARM respectively.

To build the OSv kernel you need a physical or virtual machine with Linux distribution on it and GCC toolchain and
all necessary packages and libraries OSv build process depends on. The fastest way to set it up is to use the
[Docker files](https://github.com/cloudius-systems/osv/tree/master/docker#docker-osv-builder) that OSv comes with.
You can use them to build your own Docker image and then start it in order to build OSv kernel or run an app on OSv inside of it.
Please note that the main docker file depends on pre-built base **Docker images** for 
[Ubuntu](https://hub.docker.com/repository/docker/osvunikernel/osv-ubuntu-20.10-builder-base) 
or [Fedora](https://hub.docker.com/repository/docker/osvunikernel/osv-fedora-31-builder-base) 
that get published to DockerHub upon every commit. This should speed up building the final images
as all necessary packages are installed as part of the base images.

Alternatively, you can manually clone the OSv repo and use [setup.py](https://github.com/cloudius-systems/osv/blob/master/scripts/setup.py)
to install all required packages and libraries, as long as it supports your Linux distribution, and you have both git 
and python 3 installed on your machine:
```bash
git clone https://github.com/cloudius-systems/osv.git
cd osv && git submodule update --init --recursive
./scripts/setup.py
```

The `setup.py` recognizes and installs packages for a number of Linux distributions including Fedora, Ubuntu,
[Debian](https://github.com/cloudius-systems/osv/wiki/Building-OSv-on-Debian-stable), LinuxMint and RedHat ones 
(Scientific Linux, NauLinux, CentOS Linux, Red Hat Enterprise Linux, Oracle Linux). Please note that we actively
maintain and test only Ubuntu and Fedora, so your mileage with other distributions may vary. The support of CentOS 7
has also been recently added and tested so it should work as well. The `setup.py`
is used by Docker files internally to achieve the same result. 

### IDEs

If you like working in IDEs, we recommend either [Eclipse CDT](https://www.eclipse.org/cdt/) which can be setup
as described in this [wiki page](https://github.com/cloudius-systems/osv/wiki/Working-With-Eclipse-CDT) or 
[CLion from JetBrains](https://www.jetbrains.com/clion/) which can be set to work with OSv makefile using
so-called compilation DB as described in this [guide](https://www.jetbrains.com/help/clion/managing-makefile-projects.html).

## Building OSv Kernel and Creating Images

Building OSv is as easy as using the shell script `./scripts/build`
that orchestrates the build process by delegating to the main [makefile](https://github.com/cloudius-systems/osv/blob/master/Makefile)
to build the kernel and by using a number of Python scripts like `./scripts/module.py` 
to build application and *fuse* it together with the kernel
into a final image placed at `./build/release/usr.img` (or `./build/$(arch)/usr.img` in general).
Please note that *building an application* does not necessarily mean building from sources as in many 
cases the application binaries would be located on and copied from the Linux build machine
using the shell script `./scripts/manifest_from_host.sh`
(see [this Wiki page](https://github.com/cloudius-systems/osv/wiki/Running-unmodified-Linux-executables-on-OSv) for details).

The shell script `build` can be used as the examples below illustrate:
```bash
# Create a default image that comes with a command line and REST API server
./scripts/build

# Create an image with native-example app
./scripts/build -j4 fs=rofs image=native-example

# Create an image with spring boot app with Java 10 JRE
./scripts/build JAVA_VERSION=10 image=openjdk-zulu-9-and-above,spring-boot-example

 # Create an image with 'ls' executable taken from the host
./scripts/manifest_from_host.sh -w ls && ./scripts/build --append-manifest

# Create a test image and run all tests in it
./scripts/build check

# Clean the build tree
./scripts/build clean
```

Command nproc will calculate the number of jobs/threads for make and `./scripts/build` automatically.
Alternatively, the environment variable MAKEFLAGS can be exported as follows:

```
export MAKEFLAGS=-j$(nproc)
```

In that case, make and scripts/build do not need the parameter -j.

For details on how to use the build script, please run `./scripts/build --help`.

The `./scripts/build` creates the image `build/last/usr.img` in qcow2 format.
To convert this image to other formats, use the `./scripts/convert`
tool, which can convert an image to the vmdk, vdi or raw formats.
For example:

```
./scripts/convert raw
```

### Aarch64

By default, the OSv kernel gets built for the native host architecture (x86_64 or aarch64), but it is also possible
 to cross-compile kernel and modules on Intel machine for ARM by adding **arch** parameter like so:
```bash
./scripts/build arch=aarch64
```
At this point cross-compiling the **aarch64** version of OSv is only supported
on Fedora, Ubuntu, and CentOS 7, and relevant aarch64 gcc and libraries' binaries can be downloaded using
the `./scripts/download_aarch64_packages.py` script. OSv can also be built natively on Ubuntu on ARM hardware
like Raspberry PI 4, Odroid N2+, or RockPro64. 

Please note that as of the latest [0.57.0 release](https://github.com/cloudius-systems/osv/releases/tag/v0.57.0), the ARM part of OSv has been greatly improved and tested and is pretty much on par with the x86_64 port in terms of the functionality.
In addition, all unit tests and many  advanced apps like Java, golang, nginx, python, iperf3, etc can successfully run
on QEMU and Firecraker on Raspberry PI 4 and Odroid N2+ with KVM acceleration enabled.

For more information about the aarch64 port please read [this Wiki page](https://github.com/cloudius-systems/osv/wiki/AArch64).

### Filesystems

At the end of the boot process, the OSv dynamic linker loads an application ELF and any related libraries
from the filesystem on a disk that is part of the image. By default, the images built by `./scripts/build`
contain a disk formatted with the ZFS filesystem, which you can read more about [here](https://github.com/cloudius-systems/osv/wiki/ZFS).
ZFS is a great read-write file system and may be a perfect fit if you want to run MySQL on OSv. However, it may be an overkill
if you want to run stateless apps in which case you may consider 
[Read-Only FS](https://github.com/cloudius-systems/osv/commit/cd449667b7f86721095ddf4f9f3f8b87c1c414c9). Finally,
you can also have OSv read the application binary from RAMFS, in which case the filesystem gets embedded as part of
the kernel ELF. You can specify which filesystem to build the image disk with
by setting the parameter `fs` of `./scripts/build` to one of the three values -`zfs`, `rofs`, or `ramfs`.

In addition, one can mount NFS filesystem, which had been recently transformed to be a shared library pluggable as a [module](https://github.com/cloudius-systems/osv/tree/master/modules/nfs), and newly implemented and improved [Virtio-FS filesystem](https://stefanha.github.io/virtio/virtio-fs.html#x1-41500011). The Virtio-FS mounts can be set up by adding proper entry `/etc/fstab` or by passing a boot parameter as explained in this [Wiki](https://github.com/cloudius-systems/osv/wiki/virtio-fs). In addition, very recently OSv has been enhanced to be able to boot from Virtio-FS filesystem directly.

Moreover, we have added support for the ext2/3/4 filesystem, in the form of a shared pluggable module [`libext`](https://github.com/cloudius-systems/osv/tree/master/modules/libext). One can add the `libext` module to an image and have OSv mount the ext filesystem from a separate disk like so (for more detailed examples please read [here](https://github.com/cloudius-systems/osv/tree/master/modules/libext#building-image-with-ext4-support)):
```bash
./scripts/build fs=rofs image=libext,native-example

./scripts/run.py --execute='--mount-fs=ext,/dev/vblk1,/data /hello' --second-disk-image ./ext4.img
```

Finally, the ZFS support has been also greatly improved as of the 0.57 release and there are many methods and setups to build and run ZFS images with OSv. For details please read the ZFS section of the [Filesystems wiki](https://github.com/cloudius-systems/osv/wiki/Filesystems#zfs).

## Running OSv

Running an OSv image, built by `scripts/build`, is as easy as:
```bash
./scripts/run.py
```

By default, the `run.py` runs OSv under KVM, with 4 vCPUs and 2 GB of memory. 
You can control these and tens of others ones by passing relevant parameters to 
the `run.py`. For details, on how to use the script, please run `./scripts/run.py --help`.

The `run.py` can run an OSv image on QEMU/KVM, Xen, and VMware. If running under KVM you can terminate by hitting Ctrl+A X.

Alternatively, you can use `./scripts/firecracker.py` to run OSv on [Firecracker](https://firecracker-microvm.github.io/). 
This script automatically downloads firecracker binary if missing, and accepts several parameters like the number of vCPUs, and memory
named exactly like `run.py` does. You can learn more about running OSv on Firecracker 
from this [wiki](https://github.com/cloudius-systems/osv/wiki/Running-OSv-on-Firecracker). 

Please note that to run OSv with the best performance on Linux under QEMU or Firecracker you need KVM enabled 
(this is only possible on *physical* Linux machines, EC2 "bare metal" (i3) instances, or VMs that support nested virtualization with KVM on). 
The easiest way to verify if KVM is enabled is to check if `/dev/kvm` is present, and your user account can read from and write to it. 
Adding your user to the kvm group may be necessary like so:
```bash
usermod -aG kvm <user name>
```

For more information about building and running JVM, Node.JS, Python, and other managed runtimes as well as Rust, Golang, or C/C++ apps
on OSv, please read this [wiki page](https://github.com/cloudius-systems/osv/wiki#running-your-application-on-osv). 
For more information about various example apps you can build and run on OSv, please read 
[the osv-apps repo README](https://github.com/cloudius-systems/osv-apps#osv-applications).

### Application Types and Launch Modes
Regarding how applications are launched on OSv, they all fall into two categories - **dynamically linked** and **statically linked** executables. The dynamically linked executables can be launched by the OSv built-into-kernel dynamic linker or the Linux dynamic linker `ld*.so`. The statically linked executables are bootstrapped but OSv dynamic linker but then interact via system calls with OSv kernel. For more details please watch the 1st half of [this presentation](https://fosdem.org/2024/schedule/event/fosdem-2024-3483-support-dynamically-linked-executables-via-linux-ld-so-and-implement-ena-driver-to-expand-application-of-osv/) or read [slides 2-7](https://fosdem.org/2024/events/attachments/fosdem-2024-3483-support-dynamically-linked-executables-via-linux-ld-so-and-implement-ena-driver-to-expand-application-of-osv/slides/22482/OSv_FOSDEM_24_5NmcHjr.pdf).
#### Dynamically Linked Executables
The dynamically linked executables require the dynamic linker (built-in or Linux one) to bootstrap the main application ELF file, load the libraries it depends on, resolve symbols and eventually call the `main` function.
##### Via Built-in Dynamic Linker and `libc`
The built-in dynamic linker plays the role of the program interpreter that performs similar steps as on Linux, but instead of loading the libraries it depends on from filesystem, it resolves the undefined symbols by pointing them to the implementations of those in OSv built-in `libc`. The OSv linker supports both Shared Libraries and Dynamically Linked Executables that are either position dependent or non-position dependent.

```bash
./scripts/build image=native-example
./scripts/run.py -e '/hello'
```
The benefit is that programs interact with the OSv kernel using the **fast local function calls** without the overhead of SYSCALL/SVC instruction. On the negative side, the Linux-compatibility is a moving target because GLIBc keeps adding new functions, and OSv needs to keep implementing them.
##### Via Linux Dynamic Linker `ld*.so` and `glibc`
Similarly to the built-in dynamic linker, OSv can also launch dynamically linked executables via the Linux dynamic linker `ld*.so`. The Linux dynamic linker `ld*.so` is bootstrapped the exact same way as a statically linked executable (see below) and then it orchestrates loading and execution of the specified dynamically linked executables. Just like with statically linked executable, the application interacts with OSv kernel via system calls.
```bash
dl=linux ./scripts/manifest_from_host.sh /bin/ls && ./scripts/build image=empty --append-manifest
./scripts/run.py -e '/lib64/ld-linux-x86-64.so.2 /hello'
```
#### Statically Linked Executables
The statically linked executables interact with OSv kernel by directly making system calls and reading from pseudo filesystems like procfs and sysfs like in Linux. 

In this mode, the Linux-compatibility is should be improved. But compared to the dynamically linked executables that call *local functions*, the statically linked ones suffer from the ~110 ns system call overhead mainly paid to save and restore the state of regular registers and FPU. Having said that, most Linux applications have been written with the understanding that system calls are expensive and avoid them if possible so neither statically linked executables are affected negatively nor the dynamically linked ones launched via built-in dynamic linker benefit in any significant way.

For more information about OSv implementet syscalls please read this [wiki](https://github.com/cloudius-systems/osv/wiki/Syscalls).

### Networking

By default, the `run.py`  starts OSv with
 [user networking/SLIRP](https://wiki.qemu.org/Documentation/Networking#User_Networking_.28SLIRP.29) on. 
To start OSv with more performant external networking, you need to enable `-n` and `-v` options like so:

```
sudo ./scripts/run.py -nv
```

The -v is for KVM's vhost that provides better performance
and its setup requires tap device thus we use sudo.

Alternatively, one can run OSv as a non-privileged used with a tap device like so:
```
./scripts/create_tap_device.sh natted qemu_tap0 172.18.0.1 #You can pick a different address but then update all IPs below

./scripts/run.py -n -t qemu_tap0 \
  --execute='--ip=eth0,172.18.0.2,255.255.255.252 --defaultgw=172.18.0.1 --nameserver=172.18.0.1 /hello'
```

By default, OSv spawns a `dhcpd`-like thread that automatically configures virtual NICs.
A static configuration can be done within OSv by configuring networking like so:

```
ifconfig virtio-net0 192.168.122.100 netmask 255.255.255.0 up
route add default gw 192.168.122.1
```

To enable networking on Firecracker, you have to explicitly enable `-n` option
to `firecracker.py`.

Finally, please note that the master branch of OSv only implements IPV4 subset of the networking stack.
If you need IPV6, please build from [ipv6 branch](https://github.com/cloudius-systems/osv/tree/ipv6)
 or use IPV6 kernel published to [nightly releases repo](https://github.com/osvunikernel/osv-nightly-releases/releases/tag/ci-ipv6-latest). 

## Debugging, Monitoring, Profiling OSv

- OSv can be debugged with gdb; for more details please read this
 [wiki](https://github.com/cloudius-systems/osv/wiki/Debugging-OSv)
- OSv kernel and application can be traced and profiled; for more details please read 
this [wiki](https://github.com/cloudius-systems/osv/wiki/Trace-analysis-using-trace.py)
- OSv comes with the admin/monitoring REST API server; for more details please read 
[this](https://github.com/cloudius-systems/osv/wiki/Command-Line-Interface-(CLI)) and
 [that wiki page](https://github.com/cloudius-systems/osv/wiki/Using-OSv-REST-API). There is also
 lighter [monitoring REST API module](https://github.com/cloudius-systems/osv/commit/aa32614221254ce300f401bb99c506b528b85682) 
 that is effectively a read-only subset of the former one. 
 
## FAQ and Contact

If you want to learn more about OSv or ask questions, 
please contact us on [OSv Google Group forum](https://groups.google.com/forum/#!forum/osv-dev).
You can also follow us on [Twitter](https://twitter.com/osv_unikernel).

## Papers and Articles about OSv

List of somewhat newer articles about OSv found on the Web:
* [P99 Presentation: OSv Unikernel — Optimizing Guest OS to Run Stateless and Serverless Apps in the Cloud](https://www.p99conf.io/session/osv-unikernel-optimizing-guest-os-to-run-stateless-and-serverless-apps-in-the-cloud/)
* [Unikernels vs Containers: An In-Depth Benchmarking Study in the context of Microservice Applications](https://biblio.ugent.be/publication/8582433/file/8582438)
* [Towards a Practical Ecosystem of Specialized OS Kernels](http://cs.iit.edu/~khale/docs/diver-ross19.pdf)
* [A Performance Evaluation of Unikernels](https://pdfs.semanticscholar.org/d956/f72dbc65301578dc95e0f751f4ae7c09d831.pdf)
* [Security Perspective on Unikernels](https://arxiv.org/pdf/1911.06260.pdf)
* [Performance Evaluation of OSv for Server Applications](http://www.cs.utah.edu/~peterm/prelim-osv-performance.pdf)
* [Time provisioning Evaluation of KVM, Docker and Unikernels in a Cloud Platform](https://tiagoferreto.github.io/pubs/2016ccgrid_xavier.pdf)
* [Unikernels - Beyond Containers to the Next Generation of the Cloud](https://theswissbay.ch/pdf/_to_sort/O'Reilly/unikernels.pdf)

### FOSDEM Presentations
* [2024 - Support Dynamically Linked Executables via Linux ld.so and Implement ENA Driver to Expand Application of OSv](https://fosdem.org/2024/schedule/event/fosdem-2024-3483-support-dynamically-linked-executables-via-linux-ld-so-and-implement-ena-driver-to-expand-application-of-osv/)
* [2023 - Evolution of OSv: Towards Greater Modularity and Composability](https://archive.fosdem.org/2023/schedule/event/osvevolution/)
* [2014 - OSv, a New Operating System Designed for the Cloud](https://archive.fosdem.org/2014/schedule/event/virtiaas99/)

You can find some older articles and presentations at http://osv.io/resources and http://blog.osv.io/.
