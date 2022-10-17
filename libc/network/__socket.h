#include <sys/socket.h>
//This header is included by musl src/network/if_indextoname.c and src/network/if_nametoindex.c
//to force that socket is opened using AF_INET family as AF_UNIX is not supported
#undef AF_UNIX
#define AF_UNIX AF_INET
