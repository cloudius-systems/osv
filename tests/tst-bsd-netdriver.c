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
#include <stdio.h>
#include <unistd.h>
#include <osv/debug.h>

#include <bsd/porting/netport.h>
#include <bsd/porting/networking.h>
#include <bsd/porting/route.h>
#include <bsd/porting/callout.h>

#include <bsd/sys/sys/param.h>
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
#define TLOG(...) tprintf("tst-bsd-netdriver", logger_debug, __VA_ARGS__)

/* Global ifnet */
struct ifnet* pifp;

static u_char if_eaddr[ETHER_ADDR_LEN] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
static char *if_eaddr_cstr = "11:22:33:44:55:66";
static char *gw_eaddr_cstr = "77:22:33:44:55:66";
static char *if_name = "tst-netdriver";
static char *if_name1 = "tst-netdriver0";
static char *if_ip = "198.0.0.4";
static char *if_gw = "198.0.0.1";
static char *if_baddr = "198.0.0.255";
static int masklen = 24;

static struct callout fake_isr;

/*
 * Dump a byte into hex format.
 */
static void
hexbyte(char *buf, uint8_t temp)
{
    uint8_t lo;
    uint8_t hi;

    lo = temp & 0xF;
    hi = temp >> 4;

    if (hi < 10)
        buf[0] = '0' + hi;
    else
        buf[0] = 'A' + hi - 10;

    if (lo < 10)
        buf[1] = '0' + lo;
    else
        buf[1] = 'A' + lo - 10;
}

/*
 * Display a region in traditional hexdump format.
 */
static void
hexdump(const uint8_t *region, uint32_t len)
{
    const uint8_t *line;
    char linebuf[128];
    int i;
    int x;
    int c;

    for (line = region; line < (region + len); line += 16) {

        i = 0;

        linebuf[i] = ' ';
        hexbyte(linebuf + i + 1, ((line - region) >> 8) & 0xFF);
        hexbyte(linebuf + i + 3, (line - region) & 0xFF);
        linebuf[i + 5] = ' ';
        linebuf[i + 6] = ' ';
        i += 7;

        for (x = 0; x < 16; x++) {
          if ((line + x) < (region + len)) {
            hexbyte(linebuf + i,
                *(const u_int8_t *)(line + x));
          } else {
              linebuf[i] = '-';
              linebuf[i + 1] = '-';
            }
            linebuf[i + 2] = ' ';
            if (x == 7) {
              linebuf[i + 3] = ' ';
              i += 4;
            } else {
              i += 3;
            }
        }
        linebuf[i] = ' ';
        linebuf[i + 1] = '|';
        i += 2;
        for (x = 0; x < 16; x++) {
            if ((line + x) < (region + len)) {
                c = *(const u_int8_t *)(line + x);
                /* !isprint(c) */
                if ((c < ' ') || (c > '~'))
                    c = '.';
                linebuf[i] = c;
            } else {
                linebuf[i] = ' ';
            }
            i++;
        }
        linebuf[i] = '|';
        linebuf[i + 1] = 0;
        i += 2;
        puts(linebuf);
    }
}


/*
 * This function should invoke ether_ioctl...
 */
static int
lge_ioctl(struct ifnet        *ifp,
          u_long          command,
          caddr_t         data)
{
    TLOG("lge_ioctl(%x)", command);

    int error = 0;
    switch(command) {
    case SIOCSIFMTU:
        TLOG("SIOCSIFMTU");
        break;
    case SIOCSIFFLAGS:
        TLOG("SIOCSIFFLAGS");
        /* Change status ifup, ifdown */
        if (ifp->if_flags & IFF_UP) {
            ifp->if_drv_flags |= IFF_DRV_RUNNING;
            TLOG("if_up");
        } else {
            ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
            TLOG("if_down");
        }
        break;
    case SIOCADDMULTI:
    case SIOCDELMULTI:
        TLOG("SIOCDELMULTI");
        break;
    default:
        TLOG("redirecting to ether_ioctl()...");
        error = ether_ioctl(ifp, command, data);
        break;
    }

    return(error);
}

/*
 * Main transmit routine.
 */
static void
lge_start(struct ifnet* ifp)
{
    struct mbuf* m_head = NULL;
    struct mbuf* m = NULL;
    int frag_num = 1;
    uint8_t* frag = 0;

    TLOG("lge_start (transmit)");

    /* Process packets */
    IF_DEQUEUE(&ifp->if_snd, m_head);
    while (m_head != NULL) {
        TLOG("*** processing packet! ***");

        frag_num = 0;
        /* Process fragments */
        for (m = m_head; m != NULL; m = m->m_next) {
            if (m->m_len != 0) {
                frag = mtod(m, uint8_t*);
                printf("Frag #%d len=%d:\n", ++frag_num, m->m_len);
                hexdump(frag, m->m_len);
            }
        }

        IF_DEQUEUE(&ifp->if_snd, m_head);
    }
}

static void
lge_init(void *xsc)
{
    TLOG("lge_init");
}

int create_if(void)
{
    TLOG("[~] Creating interface...");
    pifp = if_alloc(IFT_ETHER);
    if (pifp == NULL) {
        TLOG("[-] if_alloc() failed!");
        return (-1);
    }

    if_initname(pifp, if_name, 0);
    pifp->if_mtu = ETHERMTU;
    pifp->if_softc = (void*)"Driver private softc";
    pifp->if_flags = IFF_BROADCAST /*| IFF_MULTICAST*/;
    pifp->if_ioctl = lge_ioctl;
    pifp->if_start = lge_start;
    pifp->if_init = lge_init;
    pifp->if_snd.ifq_maxlen = 2;
    pifp->if_capabilities = 0/* IFCAP_RXCSUM */;
    pifp->if_capenable = pifp->if_capabilities;

    ether_ifattach(pifp, if_eaddr);

    return (0);
}

void destroy_if(void)
{
    TLOG("[~] Destroying interface...");
    ether_ifdetach(pifp);
    if_free(pifp);
}

void test_ping(void)
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

    /* Create socket */
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
    if (error) {
        TLOG("sosend_dgram() failed %d", error);
    }

    /* Wait 5 seconds */
    sleep(5);

    soclose(s);
}

void fake_isr_fn(void *unused)
{
    struct ip* ip_h;
    struct icmp* icmp_h;
    void * buf;
    char* hardcoded = "\x11\x22\x33\x44\x55\x66" /* DST */
                      "\x77\x22\x33\x44\x55\x66" /* SRC */
                      "\x08\x00"                 /* protocol = ip */
                      /* IP */
                      "\x45\x00" /*     ver + tos  */
                      "\x00\x26" /*     len */
                      "\xDA\x6A" /*     id */
                      "\x00\x00" /*     offset */
                      "\x40\x01" /*     ttl + protocol */
                      "\x14\x67" /*     checksum */
                      "\xC6\x00\x00\x01"
                      "\xC6\x00\x00\x04"
                      /* ICMP */
                      "\x00\x00" /* echo replay */
                      "\xE1\xF5" /* csum */
                      "\xBB\xAA\x00\x00" /* icmp_id */
                      "\x41\x42\x43\x44\x45\x46\x47\x48\x49\x4A"; /* payload */

    struct mbuf* m = m_getcl(M_DONTWAIT, MT_DATA, M_PKTHDR);

    buf = mtod(m, void *);
    bcopy(hardcoded, buf, 52);
    ip_h = (struct ip*)(buf + ETHER_HDR_LEN);
    icmp_h = (struct icmp*)(buf + 34);

    /* Skip IP checksum */
    m->m_pkthdr.csum_flags = (CSUM_IP_CHECKED | CSUM_IP_VALID);
    m->m_len = 52;
    m->m_pkthdr.len = m->m_len;

    /* Compute icmp checksum */
    icmp_h->icmp_cksum = 0;
    icmp_h->icmp_cksum = in_cksum_skip(m, 52, 34);

    pifp->if_ipackets++;
    m->m_pkthdr.rcvif = pifp;

    (*pifp->if_input)(pifp, m);
}

int main(void)
{
    TLOG("BSD Net Driver Test BEGIN");

    /* Init Fake ISR */
    callout_init(&fake_isr, 1);

    /* Call our function after 1 second */
    callout_reset(&fake_isr, 4*hz, fake_isr_fn, NULL);

    /* Create interface */
    create_if();

    osv_start_if(if_name1, if_ip, if_baddr, masklen);
    osv_ifup(if_name1);

    /* Add ARP */
    osv_route_arp_add(if_name1, if_ip, if_eaddr_cstr);
    osv_route_arp_add(if_name1, if_gw, gw_eaddr_cstr);

    /* Add route */
    osv_route_add_host(if_ip, if_gw);

    /* Send ICMP Packet */
    test_ping();

    /* Wait for async stuff */
    sleep(8);

    destroy_if();

    TLOG("BSD Net Driver Test END");
    return (0);
}

#undef TLOG
