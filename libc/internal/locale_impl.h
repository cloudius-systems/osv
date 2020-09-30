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

typedef struct __locale_struct *__locale_t;

typedef __locale_t locale_t;

extern __thread locale_t __current_locale;
#define CURRENT_LOCALE (__current_locale)

#define LCTRANS(msg, lc, loc) (msg) // We are not translating anything
#define LCTRANS_CUR(msg) (msg)

extern locale_t __c_locale_ptr;
#define C_LOCALE (__c_locale_ptr)
#define UTF8_LOCALE (__c_locale_ptr) // This should point to different locale struct but for now to get things going

#endif
