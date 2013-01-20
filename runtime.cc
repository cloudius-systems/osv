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
    void __assert_fail(const char * assertion, const char * file, unsigned int line, const char * function);
    __locale_t __newlocale(int __category_mask, __const char *__locale,
			   __locale_t __base) __THROW;
    double __strtod_l(__const char *__restrict __nptr,
		      char **__restrict __endptr, __locale_t __loc)
	__THROW __nonnull ((1, 3));
    float __strtof_l(__const char *__restrict __nptr,
		     char **__restrict __endptr, __locale_t __loc)
	__THROW __nonnull((1, 3)) __wur;
    char *__nl_langinfo_l(nl_item __item, __locale_t __l);
    wint_t __towlower_l(wint_t __wc, __locale_t __locale) __THROW;
    int __wcscoll_l(__const wchar_t *__s1, __const wchar_t *__s2,
		    __locale_t __loc) __THROW;
    int __strcoll_l(__const char *__s1, __const char *__s2, __locale_t __l)
	__THROW __attribute_pure__ __nonnull((1, 2, 3));
    size_t __wcsftime_l(wchar_t *__restrict __s, size_t __maxsize,
			__const wchar_t *__restrict __format,
			__const struct tm *__restrict __tp,
			__locale_t __loc) __THROW;
    int mallopt(int param, int value);
}

void *__dso_handle;

void ignore_debug_write(const char *msg)
{
}

void (*debug_write)(const char *msg) = ignore_debug_write; //replace w/ 'debug'

#define WARN(msg) do {					\
        static bool _x;					\
	if (!_x) {					\
	    _x = true;					\
	    debug("WARNING: unimplemented " msg);	\
	}						\
    } while (0)

#define UNIMPLEMENTED(msg) do {				\
	WARN(msg);					\
	abort();					\
    } while (0)


#define UNIMPL(decl) decl { UNIMPLEMENTED(#decl); }
#define IGN(decl) decl { WARN(#decl " (continuing)"); }

void abort()
{
    while (true)
	processor::halt_no_interrupts();
}

void __cxa_pure_virtual(void)
{
    abort();
}

void
perror(const char* str)
{
    printf("%s: %s\n", str, strerror(errno));
}

namespace __cxxabiv1 {
    std::terminate_handler __terminate_handler = abort;

    int __cxa_guard_acquire(__guard*)
    {
	return 0;
    }

    void __cxa_guard_release(__guard*) _GLIBCXX_NOTHROW
    {
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

FILE* stdin;
FILE* stdout;
FILE* stderr;

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

int ioctl(int fd, unsigned long request, ...)
{
    UNIMPLEMENTED("ioctl");
}

int poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
    UNIMPLEMENTED("poll");
}

int fileno(FILE *fp)
{
    UNIMPLEMENTED("fileno");
}

FILE *fdopen(int fd, const char *mode)
{
    UNIMPLEMENTED("fdopen");
}

int fflush(FILE *fp)
{
    UNIMPLEMENTED("fflush");
}

int fgetc(FILE *stream)
{
    UNIMPLEMENTED("fgetc");
}

#undef getc
int getc(FILE *stream)
{
    UNIMPLEMENTED("getc");
}

int getchar(void)
{
    UNIMPLEMENTED("getchar");
}

char *gets(char *s)
{
    UNIMPLEMENTED("gets");
}

int ungetc(int c, FILE *stream)
{
    UNIMPLEMENTED("ungetc");
}

UNIMPL(int fputc(int c, FILE *stream))
UNIMPL(int fputs(const char *s, FILE *stream))
#undef putc
UNIMPL(int putc(int c, FILE *stream))
UNIMPL(int putchar(int c))

int puts(const char *s)
{
	debug(s);
	return 0;
}

int setvbuf(FILE *stream, char *buf, int mode, size_t size)
{
    debug("stub setvbuf()");
    return 0;
}
UNIMPL(size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream))
UNIMPL(size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream))
UNIMPL(wint_t fgetwc(FILE *stream))
UNIMPL(wint_t getwc(FILE *stream))
UNIMPL(wint_t ungetwc(wint_t wc, FILE *stream))

UNIMPL(int fseeko64(FILE *stream, off64_t offset, int whence))
UNIMPL(off64_t ftell(FILE *stream))
UNIMPL(FILE *fopen64(const char *path, const char *mode))
UNIMPL(off64_t ftello64(FILE *stream))
UNIMPL(wint_t fputwc(wchar_t wc, FILE *stream))
UNIMPL(wint_t putwc(wchar_t wc, FILE *stream))
UNIMPL(void __stack_chk_fail(void))
UNIMPL(void __assert_fail(const char * assertion, const char * file, unsigned int line, const char * function))

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

UNIMPL(long double strtold_l(__const char *__restrict __nptr,
			     char **__restrict __endptr, __locale_t __loc))
UNIMPL(double __strtod_l(__const char *__restrict __nptr,
			 char **__restrict __endptr, __locale_t __loc)
			 __THROW)
UNIMPL(float __strtof_l(__const char *__restrict __nptr,
			char **__restrict __endptr, __locale_t __loc)
       __THROW)
UNIMPL(size_t __wcsftime_l (wchar_t *__restrict __s, size_t __maxsize,
			    __const wchar_t *__restrict __format,
			    __const struct tm *__restrict __tp,
			    __locale_t __loc) __THROW)

long sysconf(int name)
{
    switch (name) {
    case _SC_CLK_TCK: return CLOCKS_PER_SEC;
    case _SC_PAGESIZE: return 4096; // FIXME
    case _SC_THREAD_PROCESS_SHARED: return true;
    case _SC_NPROCESSORS_ONLN: return 1; // FIXME
    case _SC_NPROCESSORS_CONF: return 1; // FIXME
    case _SC_PHYS_PAGES: return memory::phys_mem_size / memory::page_size;
    case _SC_GETPW_R_SIZE_MAX: return 1024;
    }
    debug(fmt("sysconf: unknown parameter %1%") % name);
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
    debug(fmt("confstr: unknown parameter %1%") % name);
    abort();
}

int mallopt(int param, int value)
{
    debug(fmt("mallopt: unimplemented paramater  %1%") % param);
    return 0;
}

char* __environ_array[1];
char** environ = __environ_array;
