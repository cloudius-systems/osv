#ifndef __NETWORKING_H__
#define __NETWORKING_H__

__BEGIN_DECLS

/* Interface Functions */
void osv_start_if(const char* if_name, const char* ip_addr, const char* mask_addr);

void osv_ifup(const char* if_name);

__END_DECLS

#endif /* __NETWORKING_H__ */
