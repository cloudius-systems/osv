#ifndef __NETPORT_ROUTE_H__
#define __NETPORT_ROUTE_H__


/* Routing functions */
void osv_route_add_host(const char* destination,
    const char* gateway);

void osv_route_arp_add(const char* ifname, const char* ip,
    const char* macaddr);

#endif /* __NETPORT_ROUTE_H__ */
