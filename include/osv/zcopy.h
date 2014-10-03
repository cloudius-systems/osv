#ifndef _ZCOPY_H
#define _ZCOPY_H

#ifdef __cplusplus
extern "C" {
#endif

struct zmsghdr {
    struct msghdr zm_msg;
    int zm_txfd;
    void *zm_txhandle;
    void *zm_rxhandle;
};

ssize_t zcopy_tx(int sockfd, struct zmsghdr *zm);
void zcopy_txclose(struct zmsghdr *zm);
ssize_t zcopy_rx(int sockfd, struct zmsghdr *zm);
int zcopy_rxgc(struct zmsghdr *zm);

#ifdef __cplusplus
}
#endif

#endif
