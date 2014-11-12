#include <stdlib.h>

char *secure_getenv(const char *name)
{
	return getenv(name);
}
