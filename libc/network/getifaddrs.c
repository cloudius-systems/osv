/* (C) 2013 John Spencer. released under musl's standard MIT license. */
#undef _GNU_SOURCE
#define _GNU_SOURCE
#include <ifaddrs.h>
#include <stdlib.h>
#include <net/if.h> /* IFNAMSIZ, ifreq, ifconf */
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h> /* inet_pton */
#include <unistd.h>
#include <sys/ioctl.h>
#include <netpacket/packet.h>
#include <net/if_arp.h>

typedef union {
	struct sockaddr_in6 v6;
	struct sockaddr_in v4;
	struct sockaddr_ll hw;
} soa;

typedef struct ifaddrs_storage {
	struct ifaddrs ifa;
	soa addr;
	soa netmask;
	soa dst;
	char name[IFNAMSIZ+1];
} stor;
#define next ifa.ifa_next

static stor* list_add(stor** list, stor** head, char* ifname)
{
	stor* curr = calloc(1, sizeof(stor));
	if(curr) {
		strcpy(curr->name, ifname);
		curr->ifa.ifa_name = curr->name;
		if(*head) (*head)->next = (struct ifaddrs*) curr;
		*head = curr;
		if(!*list) *list = curr;
	}
	return curr;
}

void freeifaddrs(struct ifaddrs *ifp)
{
	stor *head = (stor *) ifp;
	while(head) {
		void *p = head;
		head = (stor *) head->next;
		free(p);
	}
}

static void ipv6netmask(unsigned prefix_length, struct sockaddr_in6 *sa)
{
	unsigned char* hb = sa->sin6_addr.s6_addr;
	unsigned onebytes = prefix_length / 8;
	unsigned bits = prefix_length % 8;
	unsigned nullbytes = 16 - onebytes;
	memset(hb, -1, onebytes);
	memset(hb+onebytes, 0, nullbytes);
	if(bits) {
		unsigned char x = -1;
		x <<= 8 - bits;
		hb[onebytes] = x;
	}
}

static void dealwithipv6(stor **list, stor** head)
{
	FILE* f = fopen("/proc/net/if_inet6", "r");
	/* 00000000000000000000000000000001 01 80 10 80 lo
	   A                                B  C  D  E  F
	   all numbers in hex
	   A = addr B=netlink device#, C=prefix length,
	   D = scope value (ipv6.h) E = interface flags (rnetlink.h, addrconf.c)
	   F = if name */
	char v6conv[32 + 7 + 1], *v6;
	char *line, linebuf[512];
	if(!f) return;
	while((line = fgets(linebuf, sizeof linebuf, f))) {
		v6 = v6conv;
		size_t i = 0;
		for(; i < 8; i++) {
			memcpy(v6, line, 4);
			v6+=4;
			*v6++=':';
			line+=4;
		}
		--v6; *v6 = 0;
		line++;
		unsigned b, c, d, e;
		char name[IFNAMSIZ+1];
		if(5 == sscanf(line, "%x %x %x %x %s", &b, &c, &d, &e, name)) {
			struct sockaddr_in6 sa = {0};
			if(1 == inet_pton(AF_INET6, v6conv, &sa.sin6_addr)) {
				sa.sin6_family = AF_INET6;
				stor* curr = list_add(list, head, name);
				if(!curr) goto out;
				curr->addr.v6 = sa;
				curr->ifa.ifa_addr = (struct sockaddr*) &curr->addr;
				ipv6netmask(c, &sa);
				curr->netmask.v6 = sa;
				curr->ifa.ifa_netmask = (struct sockaddr*) &curr->netmask;
				/* find ipv4 struct with the same interface name to copy flags */
				stor* scan = *list;
				for(;scan && strcmp(name, scan->name);scan=(stor*)scan->next);
				if(scan) curr->ifa.ifa_flags = scan->ifa.ifa_flags;
				else curr->ifa.ifa_flags = 0;
			} else errno = 0;
		}
	}
	out:
	fclose(f);
}

int allocate_and_add_ifaddrs(stor **list, stor **head, struct if_nameindex *ii)
{
	size_t i;
	for(i = 0; ii[i].if_index || ii[i].if_name; i++) {
		stor* curr = list_add(list, head, ii[i].if_name);
		if(!curr) {
			return 0;
		}
	}
	return i;
}

int fill_mac_addrs(int sock, stor *list, struct if_nameindex *ii)
{
	stor *head;
	size_t i;

	for(head = list, i = 0;
	    head; head = (stor*)(head->next)) {
		struct sockaddr_ll *hw_addr = (struct sockaddr_ll *) &head->addr;
		head->ifa.ifa_addr = (struct sockaddr*) &head->addr;
		struct ifreq req;
		int ret;

		if (!ii[i].if_name) {
			continue;
		}

		/* zero the ifreq structure */
		memset(&req, 0, sizeof(req));

		/* fill in the interface name */
		strlcpy(req.ifr_name, ii[i].if_name, IFNAMSIZ);

		/* Get the hardware address */
		ret = ioctl(sock, SIOCGIFHWADDR, &req);
		if (ret == -1) {
			return ret;
		}

		/* Fill address, index, type, length, familly */
		memcpy(hw_addr->sll_addr, req.ifr_hwaddr.sa_data, IFHWADDRLEN);
		hw_addr->sll_ifindex  = ii[i].if_index;
		hw_addr->sll_hatype = ARPHRD_ETHER;
		hw_addr->sll_halen  = IFHWADDRLEN;
		hw_addr->sll_family = AF_PACKET;

		/* Get and fill the address flags */
		ret = ioctl(sock, SIOCGIFFLAGS, &req);
		if (ret == - 1) {
			return ret;
		}
		head->ifa.ifa_flags = req.ifr_flags;
		i++;
	}

	return 0;
}

int getifaddrs(struct ifaddrs **ifap)
{
	stor *list = 0, *head = 0;
	struct if_nameindex* ii = if_nameindex();
	if(!ii) return -1;
	int addr_count;
	size_t i;
	// allocate and add to the list twice the number of
	// interface of ifaddr storage in order to have enough
	// for MAC HW addresses that require their own location.
	for (i = 0; i < 2; i++) {
		addr_count =
			allocate_and_add_ifaddrs(&list, &head, ii);
		if (!addr_count) {
			if_freenameindex(ii);
			goto err2;
		}
	}
	if_freenameindex(ii);

	int sock = socket(PF_INET, SOCK_DGRAM|SOCK_CLOEXEC, IPPROTO_IP);
	if(sock == -1) goto err2;
	struct ifreq reqs[32]; /* arbitrary chosen boundary */
	struct ifconf conf = {.ifc_len = sizeof reqs, .ifc_req = reqs};
	if(-1 == ioctl(sock, SIOCGIFCONF, &conf)) goto err;
	size_t reqitems = conf.ifc_len / sizeof(struct ifreq);
	int j;
	// loop and fill the INET addrs
	for(head = list, j = 0; head && j < addr_count; head = (stor*)head->next, j++) {
		for(i = 0; i < reqitems; i++) {
			// get SIOCGIFADDR of active interfaces.
			if(!strcmp(reqs[i].ifr_name, head->name)) {
				head->addr.v4 = *(struct sockaddr_in*)&reqs[i].ifr_addr;
				head->ifa.ifa_addr = (struct sockaddr*) &head->addr;
				break;
			}
		}
		struct ifreq req;
		snprintf(req.ifr_name, sizeof req.ifr_name, "%s", head->name);
		if(-1 == ioctl(sock, SIOCGIFFLAGS, &req)) goto err;

		head->ifa.ifa_flags = req.ifr_flags;
		if(head->ifa.ifa_addr) {
			/* or'ing flags with IFF_LOWER_UP on active interfaces to mimic glibc */
			head->ifa.ifa_flags |= IFF_LOWER_UP; 
			if(-1 == ioctl(sock, SIOCGIFNETMASK, &req)) goto err;
			head->netmask.v4 = *(struct sockaddr_in*)&req.ifr_netmask;
			head->ifa.ifa_netmask = (struct sockaddr*) &head->netmask;
	
			if(head->ifa.ifa_flags & IFF_POINTOPOINT) {
				if(-1 == ioctl(sock, SIOCGIFDSTADDR, &req)) goto err;
				head->dst.v4 = *(struct sockaddr_in*)&req.ifr_dstaddr;
			} else {
				if(-1 == ioctl(sock, SIOCGIFBRDADDR, &req)) goto err;
				head->dst.v4 = *(struct sockaddr_in*)&req.ifr_broadaddr;
			}
			head->ifa.ifa_ifu.ifu_dstaddr = (struct sockaddr*) &head->dst;
		}
	}
	if (-1 == fill_mac_addrs(sock, head, ii)) {
		goto err;
	}
	close(sock);
	void* last = 0;
	for(head = list; head; head=(stor*)head->next) last=head;
	head = last;
	dealwithipv6(&list, &head);
	*ifap = (struct ifaddrs*) list;
	return 0;
	err:
	close(sock);
	err2:
	freeifaddrs((struct ifaddrs*) list);
	return -1;
}

