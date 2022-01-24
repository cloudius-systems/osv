# Intro
The files in this subdirectory and musl C source files referenced in the [main Makefile](/Makefile) constitute the subset of libc implementation in OSv. Most of the network related functions (`bind()`, `listen()`, `accept()`, `socket()`, etc) are actually located under the `bsd/` part of the tree (see  [`/bsd/sys/kern/uipc_syscalls_wrap.cc`](/bsd/sys/kern/uipc_syscalls_wrap.cc) as an example). Please note that our libc implementation aims to be **glibc** (GNU libc) compatible even though much of the implementation comes from musl. For more details please read the [Linux ABI Compatibility](https://github.com/cloudius-systems/osv/wiki/OSv-Linux-ABI-Compatibility) and the [Components of OSv](https://github.com/cloudius-systems/osv/wiki/Components-of-OSv) wikis.

Please note that because OSv is a unikernel, much of its libc functionality has been implemented from scratch (all the C++ files in this directory). In ideal world the source files would either come from musl *as-is* or be implemented natively in OSv. But in reality some of the files in this directory originate from musl and have been adapted to make it work with OSv internals for following major reasons:
* `syscall()` invocations in many `stdio`, `stdlib`, `network` functions have been replaced with direct local functions like `SYS_open(filename, flags, ...)` to `open(filename, flags, ...)` (see [syscall_to_function.h](libc/syscall_to_function.h) for details).
* the locking logic in some of the files in musl stdio have been tweaked to use OSv mutexes

# History
Original commit

Previous is 0.9.12

# State
The current version of musl that OSv uses is 1.1.24. Most of the musl C files (`grep -P '^musl \+=' Makefile` - 577 files at this point) are directly referenced in the main makefile via a [`musl/`](/musl) symlink that currently points to the [`/musl_1.1.24`](/musl_1.1.24) git subproject. Some of the musl files are also symlinked from [`/libc`](/libc) subdirectory. Please also note that most header files under [`/include/api`](/include/api) symlink to the the musl copies under [`musl/include`](/musl/include) directory, but some are actually modified copies of original musl files. The internal musl headers under [`/include/api/internal_musl_headers`](/include/api/internal_musl_headers) symlink to files under [`musl/src/include`](/musl/src/include].

All C++ (`*.cc/*.hh`) files under `libc/` have been natively implemented in OSv. Also all FORTIFY functions for glibc compatibility (files ending with `_chk.c`) have been implemented natively.

**Following libc modules have been natively implemented in OSv as C++ files and do NOT originate from musl**:
* ldso (dynamic linker)
* malloc
* mman
* process
* sched
* signal (some implementation is from musl `as is`)
* thread
* [unistd](/libc/unistd/)
* vdso

**Following libc modules orginate from musl `as-is`**:
* crypt
* ctype
* dirent
* fenv
* math (all but `finitel.c` which is missing in musl)
* multibyte
* regex
* temp
* termios
* time (all but `__tz.c`) that comes from musl 1.1.24 and has been adapted

**Some files in the folowing libc modules originate from musl `as-is` or have been adapted from original musl sources or are original OSv implementations or originate from other open source projects**:
* [arch](/libc/arch/)
* [env](/libc/env/)
* [errno](/libc/errno/)
* [locale](/libc/locale/)
* [misc](/libc/misc/)
* [network](/libc/network/)
* [prng](/libc/prng/)
* [stdio](/libc/stdio/)
* [stdlib](/libc/stdlib/)
* [string](/libc/string/)

Files that should never change (besides C++) - TODO (list them)

Files in `libc/` subject to musl upgrade.

Possibly syslog.c might need to get updated.

# Upgrades
Any upcoming upgrade to a newer version of musl (> 1.1.24) would require at least updating
the files from mosl that have been adapded for OSv purposes (the last group, see above) and ideally
analyzing files used as-is to see if any of those need to be adapted.

Obviously we want to keep the number of adapted files to the minimum to minimize
the future upgrade efforts.
