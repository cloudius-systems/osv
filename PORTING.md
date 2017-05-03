Porting OSv
===========

1. AArch64 Port
  
  i. The AArch64 Port is being started, initially targeting the QEMU Mach-virt platform, and running on the Foundation Model v8.
  
  ii. These are some brief instructions about how to cross-compile OSv's loader.img (very incomplete still) on an X86-64 build host machine.

2. Environment Variables for Make

i. In addition to the general requirements (see README.md), note that the simple build system recognizes the CROSS_PREFIX environment variable, and looks for build tools prefixed with its contents.

ii. Alternatively, the following variables can be used to control the tools to use:

    * CXX           The target C++ compiler
    * HOST_CXX      The build host C++ compiler
    * CC            The target C compiler
    * LD            The target linker
    * STRIP         The target strip
    * OBJCOPY       The target objcopy

iii. If for some reason the ARCH-detection code does not work, it is possible to use `ARCH=aarch64` to force the target architecture to AArch64.

3. Cross-compiler from Linaro

  i. You can find a [ 32bit :-( needs multilib] crosscompiler from Linaro, in particular the package `gcc-linaro-aarch64-linux-gnu-4.8-2013.12_linux`, which is not distro-specific [ :-) ], and includes all tools needed.

  ii. For debugging purposes, a standalone gdb package for target AAarch64 can be also downloaded from the Linaro web site.

  iii. For the root filesystem for AArch64, a good option is the Linaro LEG Image linked here: [linaro-image-leg-java-genericarmv8-20131215-598.rootfs.tar.gz](https://releases.linaro.org/archive/13.12/openembedded/images/leg-java-armv8/linaro-image-leg-java-genericarmv8-20131215-598.rootfs.tar.gz).

4. Cross-compiler Tools for Ubuntu

  i. For Ubuntu there are AArch64 crosscompilers available in the official repositories as well the packages `g++-4.8-aarch64-linux-gnu` and `gcc-4.8-aarch64-linux-gnu`.
  
5. ARMv8 Foundation Model Guest Debugging

  i. In ARMv8 Foundation model it would be possible to debug the guest kernel
via the following setup:

    1. Start Foundation model with options:

       `--network=nat --network-nat-ports=1234=1234`

       The latter option will expose the gdb port used by QEMU on the host side (default port 1234) to the same port number in the guest running inside the model.

    2. If you are skipping the user space initialization via something like `init=/bin/sh` for speedup, in Foundation model you will need to run at least:

       `/sbin/udhcpc eth0`

    3. In Foundation model start qemu-system-aarch64 with the -s -S options

    4. On the host side, open gdb in a new terminal window and attach to the guest kernel with the command:

       `target remote :1234`

       This is not currently functional due to unimplemented support in KVM/QEMU.


---
This page was authored by Jani Kokkonen <jani.kokkonen@huawei.com>, Claudio Fontana <claudio.fontana@huawei.com> and edited for Markdown by Nicholas Hubbard (github:nhubbard).
