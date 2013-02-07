#include "libc.h"

static char *__initial_environ = NULL;

#undef environ
char **__environ = &__initial_environ;
weak_alias(__environ, ___environ);
weak_alias(__environ, _environ);
weak_alias(__environ, environ);
