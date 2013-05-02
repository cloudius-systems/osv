#include "sched.hh"
#include <cstdlib>
#include <cstring>
#include <string.h>
#include <exception>
#include <libintl.h>
#include <cxxabi.h>
#include <sys/mman.h>
#include <unistd.h>
#include <libunwind.h>
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
#include "processor.hh"
#include "debug.hh"
#include <boost/format.hpp>
#include "mempool.hh"
#include <pwd.h>
#include <fcntl.h>
#include "barrier.hh"
#include "smp.hh"
#include "bsd/sys/sys/sysctl.h"

#define __LC_LAST 13

typedef unsigned char __guard;

#define __alias(x) __attribute__((alias(x)))

extern "C" {
    void __cxa_pure_virtual(void);
    void abort(void);
    void _Unwind_Resume(void);
    void *malloc(size_t size);
    void free(void *);
    int tdep_get_elf_image(struct elf_image *ei, pid_t pid, unw_word_t ip,
                           unsigned long *segbase, unsigned long *mapoff,
                           char *path, size_t pathlen);
    int _Uelf64_get_proc_name(unw_addr_space_t as, int pid, unw_word_t ip,
                              char *buf, size_t buf_len, unw_word_t *offp);
    void __stack_chk_fail(void);
    __locale_t __newlocale(int __category_mask, __const char *__locale,
			   __locale_t __base) __THROW;
    char *__nl_langinfo_l(nl_item __item, __locale_t __l);
    int mallopt(int param, int value);
    FILE *popen(const char *command, const char *type);
    int pclose(FILE *stream);
}

void *__dso_handle;

#define WARN(msg) do {					\
        static bool _x;					\
	if (!_x) {					\
	    _x = true;					\
	    debug("WARNING: unimplemented " msg);	\
	}						\
    } while (0)

#define UNIMPLEMENTED(msg) do {				\
	WARN(msg "\n");					\
	abort();					\
    } while (0)


#define UNIMPL(decl) decl { UNIMPLEMENTED(#decl); }
#define IGN(decl) decl { WARN(#decl " (continuing)"); }

static bool already_aborted = false;
void abort()
{
    if (!already_aborted) {
        // Since the debug() code is complex and might cause an additional
        // abort, we need to prevent endless abort() nesting.
        debug("Aborted\n");
        already_aborted = true;
    }
    crash_other_processors();
    while (true)
	processor::halt_no_interrupts();
}

void __cxa_pure_virtual(void)
{
    abort();
}

namespace __cxxabiv1 {
    std::terminate_handler __terminate_handler = abort;

    namespace {
        struct guard {
            unsigned char initialized;
            unsigned char lock;

            int acquire() {
                if (initialized) {
                    return 0;
                }
                while (__sync_lock_test_and_set(&lock, 1)) {
                    barrier();
                }
                if (initialized) {
                    __sync_lock_release(&lock, 0);
                    return 0;
                }
                return 1;
            }

            void release() {
                initialized = 1;
                __sync_lock_release(&lock, 0);
            }
        };
    }


    int __cxa_guard_acquire(__guard* g)
    {
        return reinterpret_cast<guard*>(g)->acquire();
    }

    void __cxa_guard_release(__guard* g) _GLIBCXX_NOTHROW
    {
        return reinterpret_cast<guard*>(g)->release();
    }

    void __cxa_guard_abort(__guard*) _GLIBCXX_NOTHROW
    {
	abort();
    }

    int __cxa_atexit(void (*destructor)(void *), void *arg, void *dso)
    {
	return 0;
    }

    int __cxa_finalize(void *f)
    {
	return 0;
    }

}

int getpagesize()
{
    return 4096;
}

int
tdep_get_elf_image (struct elf_image *ei, pid_t pid, unw_word_t ip,
		    unsigned long *segbase, unsigned long *mapoff,
		    char *path, size_t pathlen)
{
    return 0;
}

int getpid()
{
    return 0;
}

uid_t getuid()
{
    return 0;
}

gid_t getgid()
{
    return 0;
}

gid_t getegid(void)
{
    return 0;
}

int mincore(void *addr, size_t length, unsigned char *vec)
{
    memset(vec, 0x01, (length + getpagesize() - 1) / getpagesize());
    return 0;
}

int _Uelf64_get_proc_name(unw_addr_space_t as, int pid, unw_word_t ip,
                          char *buf, size_t buf_len, unw_word_t *offp)
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

/*
 * Note that libstdc++ pokes into this structure, even if it is declared privately in
 * glibc, so we can't replace it with an opaque one.
 *
 * XXX: this defintion seems to be copied 1:1 from glibc, and should not stay in our
 * code if we can avoid it.  Let's figure out how libstdc++ gets at it.
 */
struct __locale_data
{
  const char *name;
  const char *filedata;		/* Region mapping the file data.  */
  off_t filesize;		/* Size of the file (and the region).  */
  enum				/* Flavor of storage used for those.  */
  {
    ld_malloced,		/* Both are malloc'd.  */
    ld_mapped,			/* name is malloc'd, filedata mmap'd */
    ld_archive			/* Both point into mmap'd archive regions.  */
  } alloc;

  /* This provides a slot for category-specific code to cache data computed
     about this locale.  That code can set a cleanup function to deallocate
     the data.  */
  struct
  {
    void (*cleanup) (struct __locale_data *);
    union
    {
      void *data;
      struct lc_time_data *time;
      const struct gconv_fcts *ctype;
    };
  } __private;

  unsigned int usage_count;	/* Counter for users.  */

  int use_translit;		/* Nonzero if the mb*towv*() and wc*tomb()
				   functions should use transliteration.  */

  unsigned int nstrings;	/* Number of strings below.  */
  union locale_data_value
  {
    const uint32_t *wstr;
    const char *string;
    unsigned int word;		/* Note endian issues vs 64-bit pointers.  */
  }
  values __flexarr;	/* Items, usually pointers into `filedata'.  */
};

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
	    ctypes[_NL_ITEM_INDEX(_NL_CTYPE_CLASS)].string + 128;
	result_ptr->__ctype_tolower = (const int *)
	    ctypes[_NL_ITEM_INDEX(_NL_CTYPE_TOLOWER)].string + 128;
	result_ptr->__ctype_toupper = (const int *)
	    ctypes[_NL_ITEM_INDEX(_NL_CTYPE_TOUPPER)].string + 128;
	return result_ptr;
    }
    abort();
}

long sysconf(int name)
{
    switch (name) {
    case _SC_CLK_TCK: return CLOCKS_PER_SEC;
    case _SC_PAGESIZE: return 4096; // FIXME
    case _SC_THREAD_PROCESS_SHARED: return true;
    case _SC_NPROCESSORS_ONLN: return sched::cpus.size();
    case _SC_NPROCESSORS_CONF: return sched::cpus.size();
    case _SC_PHYS_PAGES: return memory::phys_mem_size / memory::page_size;
    case _SC_GETPW_R_SIZE_MAX: return 1024;
    case _SC_IOV_MAX: return KERN_IOV_MAX;
    }
    debug(fmt("sysconf: unknown parameter %1%\n") % name);
    abort();
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
    abort();
}
