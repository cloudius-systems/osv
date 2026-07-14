/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Diagnostic: dump every interface address (getifaddrs) so we can see which
// IPv6 addresses (link-local vs global) got configured, e.g. by SLAAC.

#include <sys/types.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>

int main()
{
    struct ifaddrs *ifs = NULL;
    if (getifaddrs(&ifs) != 0) {
        perror("getifaddrs");
        return 1;
    }
    for (struct ifaddrs *p = ifs; p; p = p->ifa_next) {
        if (!p->ifa_addr)
            continue;
        int fam = p->ifa_addr->sa_family;
        char buf[128] = {0};
        if (fam == AF_INET) {
            auto *s = (struct sockaddr_in *)p->ifa_addr;
            inet_ntop(AF_INET, &s->sin_addr, buf, sizeof(buf));
            printf("IFADDR %s AF_INET %s\n", p->ifa_name, buf);
        } else if (fam == AF_INET6) {
            auto *s = (struct sockaddr_in6 *)p->ifa_addr;
            inet_ntop(AF_INET6, &s->sin6_addr, buf, sizeof(buf));
            const char *scope = IN6_IS_ADDR_LINKLOCAL(&s->sin6_addr) ? "link-local"
                : IN6_IS_ADDR_LOOPBACK(&s->sin6_addr) ? "loopback" : "GLOBAL";
            printf("IFADDR %s AF_INET6 %s (%s)\n", p->ifa_name, buf, scope);
        }
    }
    freeifaddrs(ifs);
    printf("ifaddr-dump done\n");
    return 0;
}
