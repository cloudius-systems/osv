This directory contains files with lists of symbols OSv kernel exports
for every library its dynamic linker advertises (see `core/elf.cc`) and
which are common across all architectures supported.
Two subdirectories - x64 and aarch64 - are intended to contain files
with lists of symbols OSv kernel exports for specific architecture
and given library. So for example the file `osv_libc.so.6.symbols`
contains an alphanumerically ordered list of symbols exported by
OSv kernel AND by the library `libc.so.6` from Linux host common
on both x86_64 and aarch64 architectures. And `x64/osv_libc.so.6.symbols`
contains symbols exported on x86_64 only.

The initial versions of these files were generated using the script `extract_symbols.sh`
like so:
```bash
./scripts/extract_symbols.sh
./scripts/extract_symbols.sh <location_of_ld-musl-x86_64.so.1>
```

Please note that the content of these files generated using the script as above
will vary by Linux distribution (Fedora, Ubuntu, etc) as the corresponding shared
libraries like `libc.so.6` may export slightly different sets of symbols. So it
is desired to generate these files on newest version of given distribution
to get largest superset of symbols for given library. In reality we just need
to use recent-enough version of Linux as OSv kernel implements finite and fewer
symbols than any of the libraries on host. Going forward as more glibc symbols are added
to OSv kernel, the corresponding files would be updated either manually or by
regenerating using the script `extract_symbols.sh`.

The files are intended to serve two purposes:
1. The files are inputs to generate a version script file that is used when linking
OSv kernel ELF with most symbols hidden except those in the version script.
2. The files document which standard glibc/musl symbols (and in future other parts of OSv ABI)
are exposed by OSv kernel.
