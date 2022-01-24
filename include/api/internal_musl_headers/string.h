#ifndef STRING_H
#define STRING_H

#include "../string.h"

hidden void *__memrchr(const void *, int, size_t);
char *__stpcpy(char *, const char *);
char *__stpncpy(char *, const char *, size_t);
hidden char *__strchrnul(const char *, int);

#endif

