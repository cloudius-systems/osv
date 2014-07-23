/*-
 * Copyright (c) 1988, 1993
 *  The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "network_interface.hh"
#include <bsd/sys/net/if_var.h>
#include <bsd/sys/net/if_types.h>

using std::string ;

const char *prog_name = "ifconfig" ;


int main(int argc, const char **argv)
{
    struct ifnet *ifp ;
    char phys_addr[64]/*, addr[16], broadcast[16], mask[16] */;

    printf("%s argc=%d argv[0]=%s\n", prog_name, argc, argv[0]) ;

    for (u_short i = 0; i <= V_if_index; i++)
    {
        ifp = ifnet_byindex_ref(i) ;

        if (ifp != NULL) {
            struct if_data cur_data = { 0 };

            osv::network::interface interface(if_name(ifp)) ;
            if (ifp->if_addr && ifp->if_addrlen && ifp->if_type == IFT_ETHER)
            {
                osv::network::link_ntoa((struct bsd_sockaddr_dl *)ifp->if_addr->ifa_addr,
                                        phys_addr) ;
            }
            else
                phys_addr[0] = '\0' ;

            assert(ifp->if_getinfo);
            ifp->if_getinfo(ifp, &cur_data);

            printf("\n") ;
            printf("%s: flags=%s  mtu %s\n",
                   interface.name.c_str(),
                   interface.flags.c_str(),
                   interface.mtu.c_str()) ;
            printf("        inet  %s  netmask %s  broadcast %s\n",
                   interface.addr.c_str(),
                   interface.mask.c_str(),
                   interface.broadcast.c_str()) ;
            if (ifp->if_type == IFT_ETHER)
                printf("        ether %s\n", phys_addr) ;
            printf("        RX packets %ld  bytes %ld %s\n",
                   cur_data.ifi_ipackets, cur_data.ifi_ibytes,
                   osv::network::interface::bytes2str(cur_data.ifi_ibytes).c_str());
            printf("        Rx errors  %ld  dropped %ld\n",
                   cur_data.ifi_ierrors, cur_data.ifi_iqdrops) ;
            printf("        TX packets %ld  bytes %ld %s\n",
                   cur_data.ifi_opackets, cur_data.ifi_obytes,
                   osv::network::interface::bytes2str(cur_data.ifi_obytes).c_str());
            printf("        Tx errors  %ld  dropped %ld collisions %ld\n",
                   cur_data.ifi_oerrors, cur_data.ifi_noproto,
                   cur_data.ifi_collisions);
            if_rele(ifp) ;
        }
    }

    return (0);
}
