#include <stdio.h>

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
#define TLOG(...) printf(__VA_ARGS__)

/* Global ifnet */
struct ifnet* pifp;

/*
 * This function should invoke ether_ioctl...
 */
static int
lge_ioctl(struct ifnet        *ifp,
          u_long          command,
          caddr_t         data)
{
    TLOG("lge_ioctl\n");

    int error = 0;
    switch(command) {
    case SIOCSIFMTU:
        break;
    case SIOCSIFFLAGS:
        break;
    case SIOCADDMULTI:
    case SIOCDELMULTI:
        break;
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
    struct mbuf     *m_head = NULL;

    TLOG("lge_start\n");

    IF_DEQUEUE(&ifp->if_snd, m_head);
    if (m_head != NULL) {
        /* Send packet */
        TLOG("Process Packet!\n");
    }
}

static void
lge_init(void *xsc)
{
    TLOG("lge_init\n");
}

int create_if(void)
{
    u_char eaddr[ETHER_ADDR_LEN] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};

    printf("[~] Creating interface!\n");
    pifp = if_alloc(IFT_ETHER);
    if (pifp == NULL) {
        printf("[-] if_alloc() failed!\n");
        return (-1);
    }

    if_initname(pifp, "test-net", 0);
    pifp->if_mtu = ETHERMTU;
    pifp->if_softc = (void*)"Driver private softc";
    pifp->if_flags = 0/* IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST */;
    pifp->if_ioctl = lge_ioctl;
    pifp->if_start = lge_start;
    pifp->if_init = lge_init;
    pifp->if_snd.ifq_maxlen = 2;
    pifp->if_capabilities = 0/* IFCAP_RXCSUM */;
    pifp->if_capenable = pifp->if_capabilities;

    ether_ifattach(pifp, eaddr);

    return (0);
}

void destroy_if(void)
{
    ether_ifdetach(pifp);
    if_free(pifp);
}

void set_address(void)
{
    struct  sockaddr_in ia_addr;
    struct ifaddr ifa;
    bzero(&ifa, sizeof(ifa));
    bzero(&ia_addr, sizeof(ia_addr));

    ifa.ifa_addr = (struct sockaddr*)&ia_addr;

    ia_addr.sin_family = AF_INET;
    ia_addr.sin_len = sizeof(struct sockaddr_in);
    /* FIXME: use inet_addr when we have one */
    ia_addr.sin_addr.s_addr = 0xAABBCCDD;

    /* This causes the arp module issue a broadcast an arp tell packet
     * (Happen only if the interface was already up! */
    ether_ioctl(pifp, SIOCSIFADDR, (caddr_t)&ifa);

}

void test_interface(void)
{
    create_if();

    /*
     * Let all domains know about this interface...
     * lo0 will be called twice...
     * (There are non configured at this moment)
     */
    if_attachdomain(NULL);

    set_address();
    destroy_if();
}

void test_sockets(void)
{
    /* ICMP Packet */
    struct mbuf *m;
    struct icmp *icp;
    char *raw;
    char *echo_payload = "ABCDEFGHIJ";

    /* Socket Variables */
    struct socket *s;
    struct sockaddr whereto;
    struct sockaddr_in *to;
    char *target = "127.0.0.1";

    /* Create socket */
    socreate(AF_INET, &s, SOCK_RAW, IPPROTO_ICMP, NULL, NULL);

    /* Setup address */
    memset(&whereto, 0, sizeof(struct sockaddr));
    whereto.sa_len = sizeof(struct sockaddr);
    to = (struct sockaddr_in *)&whereto;
    to->sin_family = AF_INET;
    inet_aton(target, &to->sin_addr);

    /* ICMP ECHO Packet */
    m = m_getcl(M_DONTWAIT, MT_DATA, M_PKTHDR);
    m->m_pkthdr.len = m->m_len = ICMP_MINLEN + 10;
    icp = mtod(m, struct icmp *);
    icp->icmp_type = ICMP_ECHO;
    icp->icmp_code = 0;
    icp->icmp_cksum = 0;
    icp->icmp_seq = 0;
    icp->icmp_id = 0xAABB;
    raw = mtod(m, char *);
    raw += ICMP_MINLEN;
    bcopy(echo_payload, raw, 10);

    /* FIXME: this actually fails since sockbuf is not ported properly
     * sbspace() return 0 since it had been stubbed
     */
    sosend_dgram(s, &whereto, NULL, m, NULL, 0, NULL);

    soclose(s);
}

int main(void)
{
    TLOG("BSD Net Driver Test BEGIN\n");

    // test_interface();
    test_sockets();

    TLOG("BSD Net Driver Test END\n");
    return (0);
}

#undef TLOG
