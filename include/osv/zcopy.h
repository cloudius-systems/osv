#ifndef _ZCOPY_H
#define _ZCOPY_H

#ifdef __cplusplus
extern "C" {
#endif

struct zmsghdr {
    struct msghdr zm_msg;
    int zm_txfd;
    void *zm_txhandle;
};

ssize_t zcopy_tx(int sockfd, struct zmsghdr *zm);
void zcopy_txclose(struct zmsghdr *zm);

#ifdef __cplusplus
}
#endif

#endif
