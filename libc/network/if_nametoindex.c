#define _GNU_SOURCE
#include <net/if.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <string.h>
#include <unistd.h>
#include "syscall.h"

unsigned if_nametoindex(const char *name)
{
	struct ifreq ifr;
	int fd, r;

	if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) return -1;
	strlcpy(ifr.ifr_name, name, sizeof ifr.ifr_name);
	r = ioctl(fd, SIOCGIFINDEX, &ifr);
	close(fd);
	return r < 0 ? r : ifr.ifr_ifindex;
}
