#ifndef LOCALE_IMPL_H
#define LOCALE_IMPL_H

#include <locale.h>

struct __locale_struct {
    struct __locale_data *__locales[13];
    const unsigned short int *__ctype_b;
    const int* __ctype_tolower;
    const int* __ctype_toupper;
    const char *__names[13];
};

typedef struct __locale_struct *__locale_t;

typedef __locale_t locale_t;

#endif
