#define _GNU_SOURCE

#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <assert.h>
#include <stdlib.h>

int gethostbyname_r(const char *name,
	struct hostent *h, char *buf, size_t buflen,
	struct hostent **res, int *err)
{
	return gethostbyname2_r(name, AF_INET, h, buf, buflen, res, err);
}


struct hostent *gethostbyname(const char *name)
{
    /* We estimate that this should be enough for the sane host names */
#define GETHOSTBYNAME_TMP_BUF_LEN  256
    static struct hostent ghbn_ret_buf;
    static char tmp_buf[GETHOSTBYNAME_TMP_BUF_LEN];

    struct hostent *tmp;
    int err, rc;

    rc = gethostbyname_r(name, &ghbn_ret_buf, tmp_buf, sizeof(tmp_buf),
                         &tmp, &err);
    assert(rc != ERANGE);

    if (rc) {
        return NULL;
    } else {
        return tmp;
    }
}
