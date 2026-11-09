# fork() on OSv

OSv is a single-address-space unikernel: all threads of all applications share
one address space, with no MMU-enforced process isolation. This is deliberate,
it is where OSv's performance comes from.

With `CONFIG_fork` enabled, OSv provides `fork()` as a new OSv thread (the
"child") that runs in its **own address space**: the parent's private writable
mappings (heap, globals, stack) become copy-on-write in both, so writes stay
private to whichever side made them, while `MAP_SHARED` and SysV shared memory
stay shared. The kernel half of the page tables is shared by every address
space, so OSv code and the kernel heap look the same from all of them. This
document describes what works, what does not, and why.

## Off by default: the `fork` configure flag

All of the fork() machinery - the fork()/vfork()/execve()/waitpid()
implementations and the per-child address space - is gated behind the
`CONFIG_fork` kconfig option (make variable `conf_fork`),
which defaults to **n**. When it is disabled (the default), NONE of this code
is compiled into the kernel: the fork object files are excluded from the build,
fork()/vfork() return ENOSYS as before, and OSv behaves exactly as it did with
no change to its single-address-space model or performance. Build with
`conf_fork=1` (or enable "Include fork() support" in `make menuconfig`) only for
workloads that need fork(). This keeps the default OSv unchanged while offering
fork() to those who require it.

## What works

- **`fork()` twin return.** `fork()` creates a new OSv thread (the "child") that
  resumes at the `fork()` call site. `fork()` returns the child's pid to the
  parent and `0` to the child, exactly like Linux. The child gets the parent's
  user stack at the **same virtual addresses** on private pages, so saved frame
  pointers, return addresses and pointers into the stack stay valid, and the
  child can unwind as deep as it likes.

- **Private memory.** The child's writes to its heap, globals and stack are not
  seen by the parent, and the parent's later writes are not seen by the child.
  `MAP_SHARED` mappings and SysV shared memory are the same pages in both.

- **`fork()` + `execve()`.** The child calls `execve()`, which launches the
  requested program as a fresh OSv application (its own ELF namespace) and makes
  the child thread that program's driver; a successful `execve()` never returns,
  as on Linux. The thread returns to the main address space and the child's
  address space is freed. The parent is a separate thread and is unaffected.

- **`waitpid()` / `wait4()` / `wait()`.** The parent reaps a child's exit status
  (Linux-encoded, use `WIFEXITED`/`WEXITSTATUS`). `WNOHANG` is supported.
  `SIGCHLD` is raised to the parent when a child exits.

- **`vfork()`.** Maps to `fork()`: the child gets a copy-on-write address space
  rather than borrowing the parent's, which a correct vfork() caller cannot tell
  apart.

- **`_exit()` / `exit()` in a child** ends only that child "process" (thread /
  app) and records its status for the parent, rather than shutting down the
  whole unikernel (which is what a top-level `exit()` still does).

## What does NOT work (and why)

- **Shared TLS (thread-local storage) - only for apps that install their own
  TCB.** A forked child is a real OSv thread, so it gets its OWN fresh, private
  OSv TLS block (its own `errno` and libc `__thread` state) automatically. For a
  program that uses OSv's (musl-derived) libc the normal way, fork() TLS "just
  works" - the child does not share the parent's TLS. The exception is a program
  that installs its OWN thread pointer via `arch_prctl(ARCH_SET_FS)` (e.g. a
  glibc-ABI binary's `__libc_setup_tls`): OSv records that as `app_tcb`, and the
  fork child currently inherits the SAME `app_tcb` (shared), which collides. The
  fix for such binaries is to build them against OSv's own musl libc instead of
  glibc, so they take the clean per-thread-TLS path. (This was the wall stock
  glibc-built PostgreSQL hit; a musl build avoids it.)

- **Other threads' stacks.** The stacks of the parent's other threads (and any
  `mmap(MAP_STACK)` region) are shared with the child, not copied: OSv runs
  kernel code on thread stacks with interrupts off, where a copy-on-write fault
  is not allowed. The child is single-threaded and normally never touches them,
  but a pointer it inherits into another thread's stack sees that thread's live
  data.

- **Isolation from the kernel.** Copy-on-write separates application memory
  only. The kernel, its heap and its data structures are shared by every
  address space, as for any OSv thread: a child is not protected from the
  kernel, or the kernel from it, the way a Linux process is.

- **`clone()` with namespace-unshare flags** (`CLONE_NEWNS`, `CLONE_NEWPID`,
  etc.) returns `ENOSYS` - there are no namespaces to unshare.

- **`fork()` issued as a raw `clone` syscall** (a statically linked program, or a
  dynamic loader that invokes the `clone` syscall directly rather than going
  through libc's `fork()`). The supported and tested path is the libc
  `fork()`/`vfork()` entry point: it captures the caller's return address and
  stack (`__builtin_return_address`/`__builtin_frame_address`) so the child
  resumes exactly at the application's `fork()` call site on its private stack.
  A `syscall(SYS_clone, ...)` with fork semantics (no `CLONE_THREAD`) is routed
  through `sys_clone()` to the same `fork()`, but the child then resumes inside
  the syscall trampoline rather than at the application's own `syscall`
  instruction, and the full user-register/`sp` state is not restored the way
  the thread `clone` path does it (`clone_thread()` carries extra assembly for
  exactly this). So a direct-`clone` fork is currently **untested and
  unsupported**; a program that expects it should be built against OSv's musl
  libc so its `fork()` takes the supported libc path. Supporting the raw-syscall
  fork properly (teaching the arch `clone`/`fork` trampoline to restore the
  caller's registers for the fork flavor) is a planned follow-up.

- **aarch64.** Not yet offered: `CONFIG_fork` depends on x86-64, so on aarch64
  `fork()` and `vfork()` return `ENOSYS` whatever `conf_fork` says. The per-child
  address space (page-table root per address space and the context-switch
  reload) is implemented for x86-64 only, and `arch/aarch64/fork.cc` uses the
  x86-64-only `sched::thread::get_app_tcb()`/`set_app_tcb()`.

## Implementation

- `libc/process/fork.cc` - `fork()`/`vfork()`, the child registry, and the
  `waitpid()` backend + `SIGCHLD` notification.
- `core/mmu.cc` - `mmu::address_space` and `clone_address_space()`: copies the
  parent's vmas into the child's address space, write-protects private
  writable pages in both for copy-on-write, and maps the forking thread's
  stack at the same addresses on private pages.
- `arch/x64/fork.cc` - `fork_thread()`: creates the child thread in the new
  address space and has it return `0` from `fork()`. Reuses the register/
  continuation approach of `clone_thread()` (used by `pthread_create`).
- `arch/x64/arch-switch.hh` - the context switch loads the target thread's
  page-table root when its address space differs from the current one.
- `libc/process/execve.cc` - `execve()` via `osv::application::run()`.
- `libc/process/waitpid.cc` - `wait`/`waitpid`/`wait4`.
- `runtime.cc` - `exit()` ends a child rather than shutting down OSv.
- `linux.cc` - `sys_clone()` routes the non-`CLONE_THREAD` (fork) case here.

## Guidance

For spawning helper programs, prefer `posix_spawn()` or `system()` (which route
straight to `osv::application::run()` and skip the stack copy entirely) over
`fork()`+`exec()` where you control the code. Use `fork()` for compatibility
with existing Linux programs that expect it, within the limitations above.
