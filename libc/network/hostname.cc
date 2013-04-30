
#include <unistd.h>
#include <string.h>

const char* default_hostname = "osv.local";

int gethostname(char* name, size_t len)
{
    strncpy(name, default_hostname, len);
    if (len > 0) {
        name[len-1] = 0;
    }
    return 0;
}

