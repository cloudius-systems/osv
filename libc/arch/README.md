* `<arch>/setjmp/sigsetjmp.s` - x64 seems to come from musl _as-is_; the aarch64 is very different from musl (seems to be a stub)
* `<arch>/setjmp/block.c` - mostly differs with `__syscall(SYS_rt_sigprocmask,..)` vs `sigprocmask()`; for whatever reason aarch64 version is different from musl (stub)
* `<arch>/setjmp/siglongjmp.c` - x64 seems to come from musl _as-is_, but changes in 1.1.24; the aarch64 is very different from musl (seems to be a stub)
* `<arch>/atomic.h`

These function seems to have been written by hand for OSv and are NOT in musl (see #8e42e07e3416ecfc7faf43e41f76262e6bc16be3 and #60fa1e93f0e1e01394cc7bd8bd1e79e34fefbfa9). Also there is no aarch64 version of it.
* `x64/ucontext/getcontext.s`
* `x64/ucontext/ucontext.cc`
* `x64/ucontext/start_context.s`
* `x64/ucontext/setcontext.s`
