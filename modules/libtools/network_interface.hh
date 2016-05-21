/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef NETWORK_INTERFACE_HH_
#define NETWORK_INTERFACE_HH_

#include <string>
#include <bsd/sys/net/if_data.h>

struct bsd_ifreq;
struct socket;
struct bsd_sockaddr_dl;
struct ifnet;

namespace osv {
namespace network {


class interface
{
public:
    //! Class constructor
    interface(const std::string& iface_name);
    std::string name;
    std::string addr;

    //! Get ip mask
    std::string mask;

    //! Get ip broadcast address
    std::string broadcast;

    //! Get interface flags
    std::string flags;

    //! Get mtu
    std::string mtu;

    // Mac address
    std::string phys_addr;

    static std::string bytes2str(long bytes);

private:
    struct socket *sock;
};
char *
link_ntoa(const struct bsd_sockaddr_dl *sdl, char obuf[64]);

unsigned short int number_of_interfaces();

struct ifnet* get_interface_by_index(unsigned int i);

struct ifnet* get_interface_by_name(const std::string& name);

std::string get_interface_name(struct ifnet* ifp);

bool set_interface_info(struct ifnet* ifp, if_data& data, interface& intf);
}
}
#endif /* NETWORK_INTERFACE_HH_ */
