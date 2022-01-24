Possibly upgrade files in `libc/network` folder when merging ipv6 branch into master.

* `freeaddrinfo.o` - symlink to musl 0.9.12, in 1.1.24 more than LOCK, mess with changes to ai_buf struct (affects `getaddrinfo.c`_
* `getaddrinfo.c` - originates from musl but some changes were made on OSv side
* `gethostbyname_r.c` - originates from musl and is identical except for extra `gethostbyname()` that can probably be moved to a separate file
* `getifaddrs.c` - originates from musl but significantly changed on OSv in commits #41fc1499ed9cdf4f876135a723e5b0fa98453161 and #e9f12c3f35aa22db1c947690e5049b52b6246381
* `getnameinfo.c` - originates from musl but makes a change to first read `/etc/services`; look at #2c7f0a58b98abdf12d17c4ac71e334db4512b72f
* `__ipparse.c` - includes `__dns.hh` instead of `__dns.h`; removed from 1.1.24

These changed between 0.9.12, in 1.1.24 and these changes seem to have been at least partially addressed in ipv6 branch
* `if_indextoname.c` - `socket(AF_UNIX,..)` vs `socket(AF_INET,..)` and `syscall(close`
* `if_nameindex.c` - ...
* `if_nametoindex.c` - ... AND `strncpy` vs `strlcpy`

Ideally can be addressed without copying into osv tree by using socket and syscall macro overrides. The strncpy vs strlcpy situation is more tricky.

30 files are from musl 'as is' (`grep -P '^musl \+= network' Makefile`)
