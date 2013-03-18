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

#define _KERNEL

#include <stdio.h>
#include <unistd.h>

#include <osv/debug.h>
#include <bsd/porting/netport.h>
#include <bsd/porting/networking.h>
#include <bsd/porting/route.h>
#include <bsd/porting/callout.h>

#include <bsd/sys/net/if_var.h>
#include <bsd/sys/net/if.h>
#include <bsd/sys/net/if_arp.h>
#include <bsd/sys/net/ethernet.h>
#include <bsd/sys/net/if_dl.h>
#include <bsd/sys/net/if_types.h>
#include <bsd/sys/netinet/in.h>
#include <bsd/sys/netinet/in_var.h>
#include <bsd/sys/netinet/ip.h>
#include <bsd/sys/netinet/ip_icmp.h>
#include <bsd/sys/sys/sockio.h>
#include <bsd/sys/sys/socket.h>
#include <bsd/sys/sys/socketvar.h>
#include <bsd/machine/in_cksum.h>

/* Test log */
#define TLOG(...) tprintf("tst-virtionet", logger_debug, __VA_ARGS__)

//static u_char if_eaddr[ETHER_ADDR_LEN] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
//static char *if_eaddr_cstr = "11:22:33:44:55:66";
static char *gw_eaddr_cstr = "52:54:00:7b:65:17";
//static char *if_name = "virtio-net";
static char *if_name1 = "virtio-net0";
static char *if_ip = "198.0.0.4";
static char *if_gw = "198.0.0.1";
static char *if_baddr = "198.0.0.255";
static int masklen = 24;

/* Try to read from the socket in test1 */
void test1_recv(struct socket* s)
{
    struct sockaddr* from;
    struct uio uio = {0};
    uio.uio_resid = 52;
    struct mbuf* m;

    soreceive_dgram(s, &from, &uio, &m, NULL, NULL);
}

/* Sends an echo request */
void test1_echorequest(void)
{
    /* ICMP Packet */
    struct mbuf *m;
    struct icmp *icp;
    char *raw;
    char *echo_payload = "ABCDEFGHIJ";

    /* Socket Variables */
    struct socket *s;
    struct sockaddr_in to, from;
    int error = -1;
    size_t sz = sizeof(struct sockaddr_in);

    error = socreate(AF_INET, &s, SOCK_RAW, IPPROTO_ICMP, NULL, NULL);
    if (error) {
        TLOG("socreate() failed %d", error);
    }

    /* Setup addresses */
    bzero(&to, sz);
    bzero(&from, sz);

    to.sin_len = sz;
    to.sin_family = AF_INET;
    inet_aton(if_gw, &to.sin_addr);

    from.sin_len = sz;
    from.sin_family = AF_INET;
    inet_aton(if_ip, &from.sin_addr);

    /* Set source address */
    error = sobind(s, (struct sockaddr *)&from, NULL);
    if (error) {
        TLOG("sobind() failed %d", error);
    }

    /* ICMP ECHO Packet */
    m = m_getcl(M_DONTWAIT, MT_DATA, M_PKTHDR);
    m->m_pkthdr.len = m->m_len = ICMP_MINLEN + 10;
    icp = mtod(m, struct icmp *);
    icp->icmp_type = ICMP_ECHO;
    icp->icmp_code = 0;
    icp->icmp_cksum = 0;
    icp->icmp_id = 0xAABB;
    icp->icmp_seq = 0;
    raw = mtod(m, char *);
    raw += ICMP_MINLEN;
    bcopy(echo_payload, raw, 10);
    icp->icmp_cksum = in_cksum(m, 18);

    /* Send an ICMP packet on our interface */
    error = sosend_dgram(s, (struct sockaddr *)&to, NULL, m, NULL, 0, NULL);
    TLOG("sosend_dgram() result is %s", error? "failure":"success");


    //test1_recv(s);

    soclose(s);
}

int main(void)
{
    const char* if_addr;
    char if_addr_cstr[6+6];
    TLOG("Virtionet Driver Test BEGIN");

    osv_start_if(if_name1, if_ip, if_baddr, masklen);
    osv_ifup(if_name1);

    if_addr = osv_get_if_mac_addr(if_name1);
    if (!if_addr) TLOG("NO ADDR FOUND");

    sprintf(if_addr_cstr,"%x:%x:%x:%x:%x:%x\n", if_addr[0], if_addr[1], if_addr[2], if_addr[3], if_addr[4], if_addr[5], if_addr[6]);
    TLOG("MAC is %s", if_addr_cstr);

    /* Add ARP */
    osv_route_arp_add(if_name1, if_ip, if_addr_cstr);
    osv_route_arp_add(if_name1, if_gw, gw_eaddr_cstr);

    /* Add route */
    osv_route_add_host(if_ip, if_gw);

    /* Send ICMP Packet */
    test1_echorequest();

    /* Wait for async stuff */
    sleep(8);

    //destroy_if();

    TLOG("Virtionet Driver Test END");
    return (0);
}

#undef TLOG
