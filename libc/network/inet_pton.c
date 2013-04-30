#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>

static int hexval(unsigned c)
{
	if (c-'0'<10) return c-'0';
	c |= 32;
	if (c-'a'<6) return c-'a'+10;
	return -1;
}

int inet_pton(int af, const char *restrict s, void *restrict a0)
{
	uint16_t ip[8];
	unsigned char *a = a0;
	const char *z;
	unsigned long x;
	int i, j, v, d, brk=-1, need_v4=0;

	/* Reimplement this because inet_pton cannot accept special v4 forms */
	if (af==AF_INET) {
		for (i=0; i<4 && *s; i++) {
			a[i] = x = strtoul(s, (char **)&z, 10);
			if (!isdigit(*s) || z==s || (*z && *z != '.') || x>255)
				return 0;
			s=z+1;
		}
		return 1;
	} else if (af!=AF_INET6) {
		errno = EAFNOSUPPORT;
		return -1;
	}

	if (s[0]==':' && s[1]==':') s++;

	for (i=0; ; i++, s+=j+1) {
		if (s[0]==':' && brk<0) {
			brk=i;
			j=0;
			ip[i]=0;
			if (!s[1]) break;
			continue;
		}
		if (hexval(s[0])<0) return -1;
		while (s[0]=='0' && s[1]=='0') s++;
		for (v=j=0; j<5 && (d=hexval(s[j]))>=0; j++)
			v=16*v+d;
		if (v > 65535) return -1;
		ip[i] = v;
		if (!s[j]) {
			if (brk<0 && i!=7) return -1;
			break;
		}
		if (i<7) {
			if (s[j]==':') continue;
			if (s[j]!='.') return -1;
			need_v4=1;
			i++;
			break;
		}
		return -1;
	}
	if (brk>=0) {
		memmove(ip+brk+7-i, ip+brk, 2*(i+1-brk));
		for (j=0; j<7-i; j++) ip[brk+j] = 0;
	}
	for (j=0; j<8; j++) {
		*a++ = ip[j]>>8;
		*a++ = ip[j];
	}
	if (need_v4 &&inet_pton(AF_INET, (void *)s, a-4) <= 0) return -1;
	return 1;
}
