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

typedef unsigned char __guard;

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
}

void *__dso_handle;

void ignore_debug_write(const char *msg)
{
}

void (*debug_write)(const char *msg) = ignore_debug_write;

#define UNIMPLEMENTED(msg) do {				\
        static bool _x;					\
	if (!_x) {					\
	    _x = true;					\
	    debug_write("WARNING: unimplemented " msg);	\
	}						\
    } while (0)


void abort()
{
    while (true)
	;
}

void operator delete(void *)
{
    UNIMPLEMENTED("operator delete");
}

void __cxa_pure_virtual(void)
{
    abort();
}

static char malloc_buffer[1 << 24], *malloc_ptr = malloc_buffer;

void *malloc(size_t size)
{
    size = (size + 7) & ~(size_t)7;
    void* ret = malloc_ptr;
    malloc_ptr += size;
    return ret;
}

void free(void* ptr)
{
}

void *memcpy(void *dest, const void *src, size_t n)
{
    char* p = reinterpret_cast<char*>(dest);
    const char* q = reinterpret_cast<const char*>(src);

    while (n--) {
	*p++ = *q++;
    }
    return dest;
}

void *memmove(void *dest, const void *src, size_t n)
{
    char* p = reinterpret_cast<char*>(dest);
    const char* q = reinterpret_cast<const char*>(src);

    if (p < q) {
	while (n--) {
	    *p++ = *q++;
	}
    } else {
	p += n;
	q += n;
	while (n--) {
	    *--p = *--q;
	}
    }
    return dest;
}

void *memset(void *s, int c, size_t n)
{
    char* p = reinterpret_cast<char*>(s);

    while (n--) {
	*p++ = c;
    }
    return s;
}

int memcmp(const void *s1, const void *s2, size_t n)
{
    const unsigned char* p1 = reinterpret_cast<const unsigned char*>(s1);
    const unsigned char* p2 = reinterpret_cast<const unsigned char*>(s2);

    while (n) {
	if (*p1 != *p2) {
	    return int(*p1) - int(*p2);
	}
	++p1;
	++p2;
	--n;
    }

    return 0;
}

const void* memchr(const void *s, int c, size_t n)
{
    const unsigned char* p = reinterpret_cast<const unsigned char*>(s);

    while (n--) {
	if (*p == c) {
	    return p;
	}
	++p;
    }

    return NULL;
}

size_t strlen(const char *s)
{
    size_t ret = 0;
    while (*s++) {
	++ret;
    }
    return ret;
}

char* strncpy(char* dest, const char* src, size_t n)
{
    char* p = dest;

    while (n--) {
        *p = *src;
        if (!*src) {
            break;
        }
        ++p;
        ++src;
    }
    return dest;
}

char* gettext (const char* msgid)
{
    return const_cast<char*>(msgid);
}

char* strerror(int err)
{
    return const_cast<char*>("strerror");
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


void *mmap(void *addr, size_t length, int prot, int flags,
	   int fd, off_t offset)
{
    if (fd != -1) {
	abort();
    }

    return malloc(length);
}

int munmap(void *addr, size_t length)
{
    return 0;
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

int mincore(void *addr, size_t length, unsigned char *vec)
{
    memset(vec, 0x01, (length + getpagesize() - 1) / getpagesize());
    return 0;
}

int dl_iterate_phdr(int (*callback)(struct dl_phdr_info *info,
                                    size_t size, void *data),
                    void *data)
{
    return 0;
}

int _Uelf64_get_proc_name(unw_addr_space_t as, int pid, unw_word_t ip,
                          char *buf, size_t buf_len, unw_word_t *offp)
{
    return 0;
}
