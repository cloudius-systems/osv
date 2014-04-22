/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/sched.hh>
#include <osv/elf.hh>
#include <cstdlib>
#include <cstring>
#include <string.h>
#include <exception>
#include <libintl.h>
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

#define __LC_LAST 13

typedef unsigned char __guard;

#define __alias(x) __attribute__((alias(x)))

extern "C" {
    void __cxa_pure_virtual(void);
    void abort(void);
    void *malloc(size_t size);
    void free(void *);
    void __stack_chk_fail(void);
    __locale_t __newlocale(int __category_mask, __const char *__locale,
			   __locale_t __base) __THROW;
    int mallopt(int param, int value);
    FILE *popen(const char *command, const char *type);
    int pclose(FILE *stream);
}

void *__dso_handle;

static void print_backtrace(void)
{
    void *addrs[128];
    int len;

    debug_ll("\n[backtrace]\n");
#ifdef AARCH64_PORT_STUB
    debug_ll("NIY\n");
    return;
#endif

    len = backtrace_safe(addrs, 128);

    /* Skip abort(const char *) and abort(void)  */
    for (int i = 2; i < len; i++) {
        auto addr = addrs[i] - 1;
        auto ei = elf::get_program()->lookup_addr(addr);
        const char *sname = ei.sym;
        char demangled[1024];

        if (!ei.sym)
            sname = "???";
        else if (demangle(ei.sym, demangled, sizeof(demangled)))
            sname = demangled;

        debug_ll("%p <%s+%d>\n",
            addr, sname,
            reinterpret_cast<uintptr_t>(addr)
            - reinterpret_cast<uintptr_t>(ei.addr));
    }
}

static bool already_aborted = false;
void abort()
{
    abort("Aborted\n");
}

void abort(const char *fmt, ...)
{
    if (!already_aborted) {
        arch::irq_disable();
        already_aborted = true;

        static char msg[1024];
        va_list ap;

        va_start(ap, fmt);
        vsnprintf(msg, sizeof(msg), fmt, ap);
        va_end(ap);

        debug_ll(msg);
        print_backtrace();
#ifndef AARCH64_PORT_STUB
        panic::pvpanic::panicked();
#endif /* !AARCH64_PORT_STUB */
    }
    osv::halt();
}

// __assert_fail() is used by the assert() macros
void __assert_fail(const char *expr, const char *file, int line, const char *func)
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
namespace __cxxabiv1 {
int __cxa_atexit(destructor_t destructor, void *arg, void *dso)
{
    // As explained above, don't remember the kernel's own destructors.
    if (dso == &__dso_handle)
        return 0;
    destructors[dso].push_back(std::make_pair(destructor, arg));
    return 0;
}

int __cxa_finalize(void *dso)
{
    if (!dso || dso == &__dso_handle) {
        debug("__cxa_finalize() running kernel's destructors not supported\n");
        return 0;
    }
    for (auto d : boost::adaptors::reverse(destructors[dso])) {
        d.first(d.second);
    }
    destructors.erase(dso);
    return 0;
}
}

int getpagesize()
{
    return 4096;
}

int vfork()
{
    debug("vfork stubbed\n");
    return -1;
}

int fork()
{
    debug("fork stubbed\n");
    return -1;
}

NO_SYS(int execvp(const char *, char *const []));
NO_SYS(int symlink(const char *, const char *));

int mlockall(int flags)
{
    return 0;
}

int munlockall(void)
{
    return 0;
}

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

static unsigned short c_locale_array[384] = {
#include "ctype-data.h"
};

static struct __locale_struct c_locale = {
    { }, // __locales_data
    c_locale_array + 128, // __ctype_b
};

UNIMPL(void __stack_chk_fail(void))

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
    abort();
}

long sysconf(int name)
{
    switch (name) {
    case _SC_CLK_TCK: return CLOCKS_PER_SEC;
    case _SC_PAGESIZE: return mmu::page_size;
    case _SC_THREAD_PROCESS_SHARED: return true;
    case _SC_NPROCESSORS_ONLN: return sched::cpus.size();
    case _SC_NPROCESSORS_CONF: return sched::cpus.size();
    case _SC_PHYS_PAGES: return memory::phys_mem_size / memory::page_size;
    case _SC_GETPW_R_SIZE_MAX: return 1024;
    case _SC_IOV_MAX: return KERN_IOV_MAX;
    case _SC_THREAD_SAFE_FUNCTIONS: return 1;
    case _SC_GETGR_R_SIZE_MAX: return 1;
    default:
        debug(fmt("sysconf(): stubbed for parameter %1%\n") % name);
        errno = EINVAL;
        return -1;
    }
}

long pathconf(const char *, int)
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

int mallopt(int param, int value)
{
    return 0;
}

FILE *popen(const char *command, const char *type)
{
    debug("popen not implemented\n");
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
void _exit(int status) __attribute((alias("exit")));
void _Exit(int status) __attribute((alias("exit")));

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
    debug("times not implemented\n");
    return 0;
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
// We (arbitrarily) want osv_prio(20) = 5, and osv_prio(-20) = 1/5.
//
// So e^(20 * prio_k) = 5
//    20 * prio_k = ln(5)
//    prio_k = ln(5) / 20
//
// When we are given OSv prio, obviously, the inverse formula applies:
//
//    e^(prio * prio_k) = osv_prio
//    prio * prio_k = ln(osv_prio)
//    prio = ln(osv_prio) / prio_k
//
static constexpr float prio_k = log(5) / 20;

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

//man getgrnam: "The return value may point to a static area, and may be
//overwritten by subsequent calls to getgrent(3), getgrgid(), or getgrnam().
//(Do not pass the returned pointer to free(3).)"
//
//OSv will recognize a few groups - as of right now, just nobody. For the
//others we will return a NULL structure, signalling an error
std::unordered_map<std::string, struct group> osv_groups  = { { "nobody", { (char *)"nobody", (char *)"", 0, nullptr } } };

struct group *getgrnam(const char *name)
{
    auto it = osv_groups.find(name);

    if (it == osv_groups.end()) {
        return nullptr;
    }
    return &it->second;
}
