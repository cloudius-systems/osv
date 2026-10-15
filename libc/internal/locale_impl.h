#ifndef LOCALE_IMPL_H
#define LOCALE_IMPL_H

#include <locale.h>
#include <stdlib.h>

struct __locale_struct {
    struct __locale_data *__locales[13];
    const unsigned short int *__ctype_b;
    const int* __ctype_tolower;
    const int* __ctype_toupper;
    const char *__names[13];
};

hidden const char *__mo_lookup(const void *, size_t, const char *);

typedef struct __locale_struct *__locale_t;

typedef __locale_t locale_t;

/* Is LC_CTYPE of l (NULL or LC_GLOBAL_LOCALE: the thread's current locale)
 * UTF-8? Defined in libc/locale/locale.cc. */
#ifdef __cplusplus
extern "C" __attribute__((visibility("hidden"))) bool __osv_ctype_utf8(locale_t) noexcept;
#define OSV_CUR_UTF8 (__osv_ctype_utf8(nullptr))
#else
__attribute__((visibility("hidden"))) _Bool __osv_ctype_utf8(locale_t);
#define OSV_CUR_UTF8 (__osv_ctype_utf8(0))
#endif

/* The loaded LC_CTYPE data (const osv_locale_data *) of l (NULL or
 * LC_GLOBAL_LOCALE: the thread's current locale), or NULL for a built-in
 * LC_CTYPE (C, C.UTF-8). Defined in libc/locale/locale.cc. */
#ifdef __cplusplus
extern "C" __attribute__((visibility("hidden"))) const void *__osv_ctype_data(locale_t) noexcept;
#else
__attribute__((visibility("hidden"))) const void *__osv_ctype_data(locale_t);
#endif

extern __thread locale_t __current_locale;
#define CURRENT_LOCALE (__current_locale)

#define LCTRANS(msg, lc, loc) (msg) // We are not translating anything
#define LCTRANS_CUR(msg) (msg)

extern locale_t __c_locale_ptr;
#define C_LOCALE (__c_locale_ptr)

/* A static C.UTF-8 locale object (musl's fwide() binds UTF-8 streams to it).
 * Defined in libc/locale/locale.cc. */
#ifdef __cplusplus
extern "C" __attribute__((visibility("hidden"))) locale_t __osv_utf8_locale(void) noexcept;
#else
__attribute__((visibility("hidden"))) locale_t __osv_utf8_locale(void);
#endif
#define UTF8_LOCALE (__osv_utf8_locale())

#endif
