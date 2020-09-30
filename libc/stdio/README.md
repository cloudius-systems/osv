All files in this folder but `*_chk.c` originate from musl and have been somewhat adapted.

# Up to date with musl 1.1.24
* `__fdopen.c` - updated
* `fmemopen.c` - updated
* `ofl.c` - completely new file from new musl
* `open_memstream.c` - updated
* `open_wmemstream.c` - updated
* `sscanf.c` - added `__attribute__((nothrow));` to `weak_alias;` only removed one include (#include "libc.h") in 1.1.24
* `vdprintf.c` - not changed in musl 1.1.24
* `vfprintf.c` - updated
* `vsnprintf.c` - updated
* `vsscanf.c` - updated
* `vswprintf.c` - updated
* `vswscanf.c` - only removed one include (#include "libc.h") in 1.1.24
* `stdio_impl.h` - modified (HOW?)

# Not updated yet
## Group 1
Comparing to 0.9.12, following files were changed to replace `f->lock < 0` with `f->no_locking` in ifs
* `getc.c` - this and below have changed in 1.1.24 to delegate to `do_getc()` (defined in getc.h)
* `fgetc.c`
* `putc.c` - this and below have changed in 1.1.24 to delegate to `do_putc()` (defined in putc.h)
* `fputc.c`
To upgrade to 1.1.24, the problem is that `do_getc()/do_putc()` in `*tc.hi` files
delegate to `locking_getc()/locking_putc()` respectively that use `MAYBE_WAITERS` macro
(see musl commit dd8f02b7dce53d6b1c4282439f1636a2d63bee01 and 9dd19122565c70bc6e0fff35724c91a61209a629)
and it is not clear if/how to update that for OSv.

## Group 2
In 1.1.24 most of the changes have been to code structure seems like and it is not clear
whether we should update it as well and how. Please see musl commit d8f2efa708a027132d443f45a8c98a0c7c1b2d77
(in essence the have made static variables for stdin, stdout and stderr as hidden. What impact could it make to us?
* `stdin.c`
* `stdout.c`
* `stderr.c`

## Group 3
Most of the changes comparing to musl copies they came from have to do with using
`mutex_*(&f->mutex)` instead of `a_cas(&f->lock...`
Musl changes between 0.9.12 and 1.1.24 are mostly around this commit c21f750727515602a9e84f2a190ee8a0a2aeb2a1
to replace waiters counter with a lock field bit.
* `__lockfile.c`
* `flockfile.c`
* `ftrylockfile.c`
* `funlockfile.c`

## Group 4
* `__fopen_rb_ca.c` - removes `O_LARGEFILE`
* `__stdio_read.c` - has a fix on OSv side, has some fixes on musl side
* `remove.c` - check for errno == EPERM, does not seem to need to be upgraded

## Others
* `getchar.c` - symlinks to 0.9.12 copy
* `putchar.c` - symlinks to 0.9.12 copy
