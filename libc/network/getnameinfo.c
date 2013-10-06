#include <osv/debug.h>
#include <netdb.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "__dns.hh"

int getnameinfo(const struct sockaddr *restrict sa, socklen_t sl,
	char *restrict node, socklen_t nodelen,
	char *restrict serv, socklen_t servlen,
	int flags)
{
	char buf[256];
	unsigned char reply[512];
	int af = sa->sa_family;
	char line[512];
	FILE *f;
	unsigned char *a;

	switch (af) {
	case AF_INET:
		a = (void *)&((struct sockaddr_in *)sa)->sin_addr;
		if (sl != sizeof(struct sockaddr_in)) return EAI_FAMILY;
		break;
	case AF_INET6:
		a = (void *)&((struct sockaddr_in6 *)sa)->sin6_addr;
		if (sl != sizeof(struct sockaddr_in6)) return EAI_FAMILY;
		break;
	default:
		return EAI_FAMILY;
	}

	/* Try to find ip within /etc/hosts */
	if ((node && nodelen) && (af == AF_INET)) {
		const char *ipstr = inet_ntoa(((struct sockaddr_in *)sa)->sin_addr);
		size_t l = strlen(ipstr);
		f = fopen("/etc/hosts", "r");
		if (f) while (fgets(line, sizeof line, f)) {
			if (strncmp(line, ipstr, l) != 0)
				continue;

			char *domain = strtok(line, " ");
			if (!domain) continue;
			domain = strtok(NULL, " ");
			if (!domain) continue;

			if (strlen(domain) >= nodelen) return EAI_OVERFLOW;
			strcpy(node, domain);
			fclose(f);
			return 0;
		}
		if (f) fclose(f);
	}

	if (node && nodelen) {
		if ((flags & NI_NUMERICHOST)
			|| __dns_query(reply, a, af, 1) <= 0
			|| __dns_get_rr(buf, 0, 256, 1, reply, RR_PTR, 1) <= 0)
		{
			if (flags & NI_NAMEREQD) return EAI_NONAME;
			inet_ntop(af, a, buf, sizeof buf);
		}
		if (strlen(buf) >= nodelen) return EAI_OVERFLOW;
		strcpy(node, buf);
	}

	if (serv && servlen) {
		if (snprintf(buf, sizeof buf, "%d",
			ntohs(((struct sockaddr_in *)sa)->sin_port))>=servlen)
			return EAI_OVERFLOW;
		strcpy(serv, buf);
	}

	return 0;
}
