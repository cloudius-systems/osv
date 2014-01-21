#include <stdlib.h>
#include <osv/debug.h>

void __assert_fail(const char *expr, const char *file, int line, const char *func)
{
	kprintf("Assertion failed: %s (%s: %s: %d)\n", expr, file, func, line);
	abort();
}
