#include <osv/debug.hh>

extern "C" void _chk_fail(const char *func)
{
    abort("%s: aborting on failed check\n", func);
}
