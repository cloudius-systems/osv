Some notes worth considering when upgrading to newer version of musl

* `freeaddrinfo.o` - symlink to musl 0.9.12, in 1.1.24 more than LOCK, mess with the changes related to the `ai_buf` struct (affects `getaddrinfo.c`)
* `getaddrinfo.c` - originates from musl but some changes were made on OSv side
* `gethostbyname_r.c` - originates from musl and is identical except for extra `gethostbyname()` that can probably be moved to a separate file
* `getnameinfo.c` - originates from musl but makes a change to first read `/etc/services`; look at #2c7f0a58b98abdf12d17c4ac71e334db4512b72f
* `__ipparse.c` - includes `__dns.hh` instead of `__dns.h`; removed from 1.1.24
