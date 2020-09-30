The only 2 files in here should stay as is.

Musl has its own copy of `env/secure_getenv.c` but it references the `libc.secure` global variable which we do not have.
