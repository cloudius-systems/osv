Both `block.c` and `siglongjmp.c` come from musl#81e18eb3cd61f7b68a8f99b3e6c728f2a4d79221 src/signal.
The `block.c` has been modified to replace system calls (`syscall`) with direct `sigprocmask()` invocation.

None of these should need to be updated when upgrading musl.
