/* Based on recent addition to Musl, see
 */

#include <stdlib.h>
#include <osv/stubbing.hh>

// We are missing an implementation of the new C23 functions strfrom[fdl]
// and eventually we can get such an implementation from Musl (see a
// proposal in https://www.openwall.com/lists/musl/2023/05/31/28), but
// for now we'll just leave these functions missing - and applications that
// try to use them will report the missing function.
//
// But for strfromf128() we need an stub now, because recent versions of
// libstdc++ started to use them. It's fine that the implementation is just
// a stub - whatever code uses the new C++ feature should fail reporting
// the unimplemented feature.
// Later, when we implement this function, we already have a test for it
// in tests/tst-f128.cc.
UNIMPL(int strfromf128(char *, size_t, const char *, __float128))

// Similarly, recent versions of libstdc++ need strtof128, but don't actually
// use it until the user really uses the __float128 type.
UNIMPL(__float128 strtof128(const char *, char **))
