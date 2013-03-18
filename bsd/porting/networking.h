#ifndef __NETWORKING_H__
#define __NETWORKING_H__

/* Interface Functions */
void osv_start_if(const char* if_name, const char* ip_addr, int masklen);

void osv_ifup(const char* if_name);


#endif /* __NETWORKING_H__ */
