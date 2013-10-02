#ifndef _ARPA_INET_H
#define	_ARPA_INET_H

#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>
#include <netinet/in.h>
#include <inttypes.h>

#define __NEED_socklen_t
#define __NEED_in_addr_t
#define __NEED_in_port_t
#define __NEED_uint16_t
#define __NEED_uint32_t
#define __NEED_struct_in_addr

#include <bits/alltypes.h>

#ifndef _BYTEORDER_FUNC_DEFINED
uint32_t htonl(uint32_t);
uint16_t htons(uint16_t);
uint32_t ntohl(uint32_t);
uint16_t ntohs(uint16_t);
#endif

in_addr_t inet_addr (const char *);
in_addr_t inet_network (const char *);
char *inet_ntoa (struct in_addr);
int inet_pton (int, const char *__restrict, void *__restrict);
const char *inet_ntop (int, const void *__restrict, char *__restrict, socklen_t);

int inet_aton (const char *, struct in_addr *); /* nonstandard but widely used */

#undef INET_ADDRSTRLEN
#undef INET6_ADDRSTRLEN
#define INET_ADDRSTRLEN  16
#define INET6_ADDRSTRLEN 46

#ifdef __cplusplus
}
#endif

#endif
