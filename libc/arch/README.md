* `<arch>/setjmp/sigsetjmp.s` - both x64 and aarch64 versions come from the musl commit 81e18eb3cd61f7b68a8f99b3e6c728f2a4d79221
* `<arch>/atomic.h`

These function seems to have been written by hand for OSv and are NOT in musl (see #8e42e07e3416ecfc7faf43e41f76262e6bc16be3 and #60fa1e93f0e1e01394cc7bd8bd1e79e34fefbfa9). Also there is no aarch64 version of it.
* `x64/ucontext/getcontext.s`
* `x64/ucontext/ucontext.cc`
* `x64/ucontext/start_context.s`
* `x64/ucontext/setcontext.s`
