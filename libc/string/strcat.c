#include <string.h>

char *strcat(char *__restrict dest, const char *__restrict src)
{
	strcpy(dest + strlen(dest), src);
	return dest;
}
