#include <sys/socket.h>
#undef MSG_DONTWAIT
//Let us disable the non-blocking call to recv() netlink.c by re-defining MSG_DONTWAIT as 0
#define MSG_DONTWAIT 0
