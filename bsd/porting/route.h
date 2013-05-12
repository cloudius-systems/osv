#ifndef __NETPORT_ROUTE_H__
#define __NETPORT_ROUTE_H__

__BEGIN_DECLS
/* Routing functions */
void osv_route_add_host(const char* destination,
    const char* gateway);

void osv_route_arp_add(const char* ifname, const char* ip,
    const char* macaddr);

const char* osv_get_if_mac_addr(const char* if_name);

int osv_sysctl(int *name, u_int namelen, void *old_buf, size_t *oldlenp,
               void *new_buf, size_t newlen) ;
__END_DECLS

#endif /* __NETPORT_ROUTE_H__ */
