#ifndef __NETWORKING_H__
#define __NETWORKING_H__

#include <sys/cdefs.h>

__BEGIN_DECLS

/* Interface Functions */
int osv_start_if(const char* if_name, const char* ip_addr, const char* mask_addr);

int osv_ifup(const char* if_name);

__END_DECLS

#endif /* __NETWORKING_H__ */
