#include <setjmp.h>

extern void __restore_sigs(void *set);

_Noreturn void siglongjmp(sigjmp_buf buf, int ret)
{
	if (buf->__fl) __restore_sigs(buf->__ss);
	longjmp(buf, ret);
}
