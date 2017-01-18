/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/sched.hh>
#include <osv/elf.hh>
#include <stdlib.h>
#include <cstring>
#include <string.h>
#include <exception>
#include <cxxabi.h>
#include <sys/mman.h>
#include <unistd.h>
#include <link.h>
#include <stdio.h>
#include <sys/poll.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <sys/uio.h>
#include <wchar.h>
#include <locale.h>
#include <libintl.h>
#include <ctype.h>
#include <wctype.h>
#include <langinfo.h>
#include <stdarg.h>
#include <xlocale.h>
#include <cassert>
#include <sys/sysinfo.h>
#include "processor.hh"
#include <osv/debug.hh>
#include <boost/format.hpp>
#include <osv/mempool.hh>
#include <pwd.h>
#include <fcntl.h>
#include <osv/barrier.hh>
#include "smp.hh"
#include "bsd/sys/sys/sysctl.h"
#include <osv/power.hh>
#include <sys/time.h>
#include <osv/mmu.hh>
#include "libc/libc.hh"
#include <api/sys/times.h>
#include <map>
#include <boost/range/adaptor/reversed.hpp>
#include <osv/align.hh>
#include <osv/stubbing.hh>
#include "drivers/pvpanic.hh"
#include <api/sys/resource.h>
#include <api/math.h>
#include <osv/shutdown.hh>
#include <osv/execinfo.hh>
#include <osv/demangle.hh>
#include <processor.hh>
#include <grp.h>
#include <unordered_map>
#include <api/sys/prctl.h>

#define __LC_LAST 13

#define __ALIAS(old, new) \
        __typeof(old) new __attribute__((alias(#old)))
#define ALIAS(old, new) extern "C" __ALIAS(old, new)

void *__dso_handle;

static void print_backtrace(void)
{
    void *addrs[128];
    int len;

    debug_ll("\n[backtrace]\n");

    len = backtrace_safe(addrs, 128);

    /* Start with i=1 to skip abort(const char *)  */
    for (int i = 1; i < len; i++) {
        char name[1024];
        void *addr = addrs[i] - INSTR_SIZE_MIN;
        osv::lookup_name_demangled(addr, name, sizeof(name));
        if (strncmp(name, "abort+", 6) == 0) {
            // Skip abort() (which called abort(const char*) already skipped
            continue;
        }
        debug_ll("0x%016lx <%s>\n", addr, name);
    }
}

static std::atomic<bool> aborting { false };

void abort()
{
    abort("Aborted\n");
}

void abort(const char *fmt, ...)
{
    bool expected = false;
    if (!aborting.compare_exchange_strong(expected, true)) {
        do {} while (true);
    }

    arch::irq_disable();

    static char msg[1024];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    debug_early(msg);
    // backtrace requires threads to be available, and also
    // ELF s_program to be initialized.
    if (sched::thread::current() && elf::get_program() != nullptr) {
        print_backtrace();
    } else {
        debug_early("Halting.\n");
    }
#ifndef AARCH64_PORT_STUB
    panic::pvpanic::panicked();
#endif /* !AARCH64_PORT_STUB */

    if (opt_power_off_on_abort) {
        osv::poweroff();
    } else {
        osv::halt();
    }
}

// __assert_fail() is used by the assert() macros
void __assert_fail(const char *expr, const char *file, unsigned int line, const char *func)
{
    abort("Assertion failed: %s (%s: %s: %d)\n", expr, file, func, line);
}


// __cxa_atexit and __cxa_finalize:
// Gcc implements static constructors and destructors in shared-objects (DSO)
// in the following way: Static constructors are added to a list DT_INIT_ARRAY
// in the object, and we run these functions after loading the object. Gcc's
// code for each constructor calls a function __cxxabiv1::__cxa_atexit (which
// we need to implement here) to register a destructor, linked to this DSO.
// Gcc also adds a single finalization function to DT_FINI_ARRAY (which we
// call when unloading the DSO), which calls __cxxabiv1::__cxa_finalize
// (which we need to implement here) - this function is supposed to call all
// the destructors previously registered for the given DSO.
//
// This implementation is greatly simplified by the assumption that the kernel
// never exits, so our code doesn't need to work during early initialization,
// nor does __cxa_finalize(0) need to work.
typedef void (*destructor_t)(void *);
static std::map<void *, std::vector<std::pair<destructor_t,void*>>> destructors;
static mutex destructors_mutex;
namespace __cxxabiv1 {
int __cxa_atexit(destructor_t destructor, void *arg, void *dso)
{
    // As explained above, don't remember the kernel's own destructors.
    if (dso == &__dso_handle)
        return 0;
    SCOPE_LOCK(destructors_mutex);
    destructors[dso].push_back(std::make_pair(destructor, arg));
    return 0;
}

int __cxa_finalize(void *dso)
{
    if (!dso || dso == &__dso_handle) {
        debug("__cxa_finalize() running kernel's destructors not supported\n");
        return 0;
    }
    std::vector<std::pair<destructor_t,void*>> my_destructors;
    WITH_LOCK(destructors_mutex) {
        my_destructors = std::move(destructors[dso]);
        destructors.erase(dso);
    }
    for (auto d : boost::adaptors::reverse(my_destructors)) {
        d.first(d.second);
    }
    return 0;
}
}

int getpagesize()
{
    return 4096;
}

int vfork()
{
    WARN_STUBBED();
    return -1;
}

int fork()
{
    WARN_STUBBED();
    return -1;
}

pid_t setsid(void)
{
    WARN_STUBBED();
    return -1;
}

NO_SYS(int execvp(const char *, char *const []));

int mlockall(int flags)
{
    WARN_STUBBED();
    return 0;
}

int munlockall(void)
{
    WARN_STUBBED();
    return 0;
}

int mlock(const void*, size_t)
{
    WARN_STUBBED();
    return 0;
}

int munlock(const void*, size_t)
{
    WARN_STUBBED();
    return 0;
}

NO_SYS(int mkfifo(const char*, mode_t));

int posix_fadvise(int fd, off_t offset, off_t len, int advice)
{
    switch (advice) {
    case POSIX_FADV_NORMAL:
    case POSIX_FADV_SEQUENTIAL:
    case POSIX_FADV_RANDOM:
    case POSIX_FADV_NOREUSE:
    case POSIX_FADV_WILLNEED:
    case POSIX_FADV_DONTNEED:
        return 0;
    default:
        return EINVAL;
    }
}
LFS64(posix_fadvise);

int posix_fallocate(int fd, off_t offset, off_t len)
{
    return ENOSYS;
}
LFS64(posix_fallocate);

int getpid()
{
    return 0;
}

//    WCTDEF(alnum), WCTDEF(alpha), WCTDEF(blank), WCTDEF(cntrl),
//    WCTDEF(digit), WCTDEF(graph), WCTDEF(lower), WCTDEF(print),
//    WCTDEF(punct), WCTDEF(space), WCTDEF(upper), WCTDEF(xdigit),

#include "ctype-data.h"

static struct __locale_struct c_locale = {
    { }, // __locales_data
    c_locale_array + 128, // __ctype_b
    c_tolower_array + 128, // __ctype_tolower
    c_toupper_array + 128, // __ctype_toupper
    { }, // __names
};

locale_t __c_locale_ptr = &c_locale;

void* __stack_chk_guard = reinterpret_cast<void*>(0x12345678abcdefull);
extern "C" void __stack_chk_fail(void) {
    abort("__stack_chk_fail(): Stack overflow detected. Aborting.\n");
}

namespace {
    bool all_categories(int category_mask)
    {
	return (category_mask | (1 << LC_ALL)) == (1 << __LC_LAST) - 1;
    }
}

struct __locale_data {
    const void *values[0];
};

#define _NL_ITEM(category, index)   (((category) << 16) | (index))
#define _NL_ITEM_CATEGORY(item)     ((int) (item) >> 16)
#define _NL_ITEM_INDEX(item)        ((int) (item) & 0xffff)

#define _NL_CTYPE_CLASS  0
#define _NL_CTYPE_TOUPPER 1
#define _NL_CTYPE_TOLOWER 3

extern "C"
__locale_t __newlocale(int category_mask, const char *locale, locale_t base)
    __THROW
{
    if (category_mask == 1 << LC_ALL) {
        category_mask = ((1 << __LC_LAST) - 1) & ~(1 << LC_ALL);
    }
    assert(locale);
    if (base == &c_locale) {
        base = NULL;
    }
    if ((base == NULL || all_categories(category_mask))
            && (category_mask == 0 || strcmp(locale, "C") == 0)) {
        return &c_locale;
    }
    struct __locale_struct result = base ? *base : c_locale;
    if (category_mask == 0) {
        auto result_ptr = new __locale_struct;
        *result_ptr = result;
        auto ctypes = result_ptr->__locales[LC_CTYPE]->values;
        result_ptr->__ctype_b = (const unsigned short *)
	            ctypes[_NL_ITEM_INDEX(_NL_CTYPE_CLASS)] + 128;
        result_ptr->__ctype_tolower = (const int *)
	            ctypes[_NL_ITEM_INDEX(_NL_CTYPE_TOLOWER)] + 128;
        result_ptr->__ctype_toupper = (const int *)
	            ctypes[_NL_ITEM_INDEX(_NL_CTYPE_TOUPPER)] + 128;
        return result_ptr;
    }
    errno = ENOENT;
    return nullptr;
}

ALIAS(__newlocale, newlocale);

long sysconf(int name)
{
    switch (name) {
    case _SC_CLK_TCK: return CLOCKS_PER_SEC;
    case _SC_PAGESIZE: return mmu::page_size;
    case _SC_THREAD_PROCESS_SHARED: return true;
    case _SC_NPROCESSORS_ONLN: return sched::cpus.size();
    case _SC_NPROCESSORS_CONF: return sched::cpus.size();
    case _SC_PHYS_PAGES: return memory::phys_mem_size / memory::page_size;
    case _SC_AVPHYS_PAGES: return memory::stats::free() / memory::page_size;
    case _SC_GETPW_R_SIZE_MAX: return 1024;
    case _SC_IOV_MAX: return KERN_IOV_MAX;
    case _SC_THREAD_SAFE_FUNCTIONS: return 1;
    case _SC_GETGR_R_SIZE_MAX: return 1;
    case _SC_OPEN_MAX: return FDMAX;
    default:
        debug(fmt("sysconf(): stubbed for parameter %1%\n") % name);
        errno = EINVAL;
        return -1;
    }
}

long pathconf(const char *, int name)
{
    return fpathconf(-1, name);
}

long fpathconf(int, int)
{
    WARN_STUBBED();
    return -1;
}

size_t confstr(int name, char* buf, size_t len)
{
    char tmp[1];
    if (!buf) {
        buf = tmp;
        len = 1;
    }
    auto set = [=] (const char* v) { return snprintf(buf, len, "%s", v); };
    switch (name) {
    case _CS_GNU_LIBC_VERSION: return set("glibc 2.16");
    case _CS_GNU_LIBPTHREAD_VERSION: return set("NPTL 2.16");
    }
    debug(fmt("confstr: unknown parameter %1%\n") % name);
    abort();
}

FILE *popen(const char *command, const char *type)
{
    WARN_STUBBED();
    return NULL;
}

int pclose(FILE *stream)
{
	return 0;
}

void exit(int status)
{
    debug(fmt("program exited with status %d\n") % status);
    osv::shutdown();
}

// "The function _exit() is like exit(3), but does not call any functions
// registered with atexit(3) or on_exit(3)."
//
// Since we do nothing for those anyway, they are equal.
ALIAS(exit, _exit);
ALIAS(exit, _Exit);

int atexit(void (*func)())
{
    // nothing to do
    return 0;
}

int get_nprocs()
{
    return sysconf(_SC_NPROCESSORS_ONLN);
}

clock_t times(struct tms *buffer)
{
    using namespace std::chrono;
    struct timespec ts;

    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);

    typedef duration<u64, std::ratio<1, CLOCKS_PER_SEC>> clockseconds;
    clockseconds time;
    time = duration_cast<clockseconds>(seconds(ts.tv_sec) + nanoseconds(ts.tv_nsec));

    buffer->tms_utime = time.count();
    buffer->tms_stime = 0;
    buffer->tms_cutime = 0;
    buffer->tms_cstime = 0;

    return buffer->tms_utime;
}

static int prio_find_thread(sched::thread **th, int which, int id)
{
    errno = 0;
    if ((which == PRIO_USER) || (which == PRIO_PGRP)) {
        *th = nullptr;
        return 0;
    }

    if (which != PRIO_PROCESS) {
        errno = EINVAL;
        return -1;
    }

    if (id == 0) {
        *th = sched::thread::current();
    } else {
        *th = sched::thread::find_by_id(id);
    }

    if (!*th) {
        errno = ESRCH;
        return -1;
    }
    return 0;
}

// Our priority formula is osv_prio = e^(prio * k), where k is a constant.
// We want osv_prio(20) = 86, and osv_prio(-20) = 1/86, as this gives the
// best agreement with Linux's current interpretation of the nice values
// (see tests/misc-setpriority.cc).
//
// So e^(20 * prio_k) = 86
//    20 * prio_k = ln(86)
//    prio_k = ln(86) / 20
//
// When we are given OSv prio, obviously, the inverse formula applies:
//
//    e^(prio * prio_k) = osv_prio
//    prio * prio_k = ln(osv_prio)
//    prio = ln(osv_prio) / prio_k
//
static constexpr float prio_k = log(86) / 20;

int getpriority(int which, int id)
{
    sched::thread *th;
    int ret = prio_find_thread(&th, which, id);
    if (ret < 0) {
        return ret;
    }

    // Case for which which is not a process and we should just return and not
    // do anything
    if (!th && !ret) {
        return 0;
    }

    // We're not super concerned with speed during get/set priority, which we
    // expect to be fairly rare. So we can use the stdlib math functions
    // instead of any fast approximations.
    int prio = logf(th->priority()) / prio_k;
    if (prio < -20) {
        prio = -20;
    }
    if (prio > 19) {
        prio = 19;
    }
    return prio;
}

int setpriority(int which, int id, int prio)
{
    sched::thread *th;
    int ret = prio_find_thread(&th, which, id);
    if (ret < 0) {
        return ret;
    }
    if (!th && !ret) {
        return 0;
    }

    float p = expf(prio_k * prio);
    th->set_priority(p);
    return 0;
}

int initgroups(const char *user, gid_t group)
{
    WARN_STUBBED();
    return -1;
}

int prctl(int option, ...)
{
    switch (option) {
    case PR_SET_DUMPABLE:
        return 0;
    }
    errno = EINVAL;
    return -1;
}

int daemon(int nochdir, int noclose)
{
    WARN_STUBBED();
    return -1;
}

extern "C"
int sysctl(int *, int, void *, size_t *, void *, size_t)
{
    WARN_STUBBED();
    errno = ENOTDIR;
    return -1;
}
