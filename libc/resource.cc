#include <sys/time.h>
#include <sys/resource.h>
#include <string.h>

#include <debug.hh>

int getrusage(int who, struct rusage *usage)
{
    debug("stub getrusage() called\n");
    memset(usage, 0, sizeof(*usage));
    return 0;
}


