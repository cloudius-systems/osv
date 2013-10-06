#include <stddef.h>

#define RR_A 1
#define RR_CNAME 5
#define RR_PTR 12
#define RR_AAAA 28

#ifdef __cplusplus
#include <vector>
#include <string>
#include <boost/asio/ip/address.hpp>

#include <osv/mutex.h>

namespace osv {

void set_dns_config(std::vector<boost::asio::ip::address> nameservers,
		    std::vector<std::string> search_domains);

std::vector<boost::asio::ip::address> get_nameservers();

std::vector<std::string> get_search_domains();

}

extern "C" {
#endif

int __dns_count_addrs(const unsigned char *, int);
int __dns_get_rr(void *, size_t, size_t, size_t, const unsigned char *, int, int);

int __dns_query(unsigned char *, const void *, int, int);
int __ipparse(void *, int, const char *);

int __dns_doqueries(unsigned char *, const char *, int *, int);

#ifdef __cplusplus
}
#endif
