/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef NETWORK_INTERFACE_HH_
#define NETWORK_INTERFACE_HH_

#include <string>


struct bsd_ifreq;
struct socket;
struct bsd_sockaddr_dl;
struct ifnet;

namespace osv {
namespace network {
struct timeval {
    long int tv_sec;
    long tv_usec;
};

/*
 * Structure describing information about an interface
 * which may be of interest to management entities.
 */
struct if_data {
    /* generic interface information */
    unsigned char  ifi_type;       /* ethernet, tokenring, etc */
    unsigned char  ifi_physical;       /* e.g., AUI, Thinnet, 10base-T, etc */
    unsigned char  ifi_addrlen;        /* media address length */
    unsigned char  ifi_hdrlen;     /* media header length */
    unsigned char  ifi_link_state;     /* current link state */
    unsigned char  ifi_spare_char1;    /* spare byte */
    unsigned char  ifi_spare_char2;    /* spare byte */
    unsigned char  ifi_datalen;        /* length of this data struct */
    unsigned long int  ifi_mtu;        /* maximum transmission unit */
    unsigned long int  ifi_metric;     /* routing metric (external only) */
    unsigned long int  ifi_baudrate;       /* linespeed */
    /* volatile statistics */
    unsigned long int  ifi_ipackets;       /* packets received on interface */
    unsigned long int  ifi_ierrors;        /* input errors on interface */
    unsigned long int  ifi_opackets;       /* packets sent on interface */
    unsigned long int  ifi_oerrors;        /* output errors on interface */
    unsigned long int  ifi_collisions;     /* collisions on csma interfaces */
    unsigned long int  ifi_ibytes;     /* total number of octets received */
    unsigned long int  ifi_obytes;     /* total number of octets sent */
    unsigned long int  ifi_imcasts;        /* packets received via multicast */
    unsigned long int  ifi_omcasts;        /* packets sent via multicast */
    unsigned long int  ifi_iqdrops;        /* dropped on input, this interface */
    unsigned long int  ifi_noproto;        /* destined for unsupported protocol */
    unsigned long int  ifi_hwassist;       /* HW offload capabilities, see IFCAP */
    time_t  ifi_epoch;      /* uptime at attach or stat reset */
    struct  timeval ifi_lastchange; /* time of last administrative change */
};


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

bool set_interface_info(struct ifnet* ifp, if_data& data);
}
}
#endif /* NETWORK_INTERFACE_HH_ */
