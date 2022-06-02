#ifndef _NETLINK_H_
#define _NETLINK_H_

#include <sys/cdefs.h>

struct nlmsghdr {
	uint32_t nlmsg_len;    /* Length of message including header */
	uint16_t nlmsg_type;   /* Type of message content */
	uint16_t nlmsg_flags;  /* Additional flags */
	uint32_t nlmsg_seq;    /* Sequence number */
	uint32_t nlmsg_pid;    /* Sender port ID */
};

struct nlmsgerr {
	int error;		/* Negative errno or 0 for ack */
	struct nlmsghdr msg;	/* Message that caused the error */
};


#define NLMSG_ALIGNTO	4U
#define NLMSG_ALIGN(len) ( ((len)+NLMSG_ALIGNTO-1) & ~(NLMSG_ALIGNTO-1) )
#define NLMSG_HDRLEN	 ((int) NLMSG_ALIGN(sizeof(struct nlmsghdr)))
#define NLMSG_LENGTH(len) ((len) + NLMSG_HDRLEN)
#define NLMSG_SPACE(len) NLMSG_ALIGN(NLMSG_LENGTH(len))
#define NLMSG_DATA(nlh)  ((void*)(((char*)nlh) + NLMSG_LENGTH(0)))
#define NLMSG_NEXT(nlh,len)	 ((len) -= NLMSG_ALIGN((nlh)->nlmsg_len),   \
                              (struct nlmsghdr*)(((char*)(nlh)) + NLMSG_ALIGN((nlh)->nlmsg_len)))
#define NLMSG_OK(nlh,len) ((len) >= (int)sizeof(struct nlmsghdr) &&     \
                           (nlh)->nlmsg_len >= sizeof(struct nlmsghdr) && \
                           (nlh)->nlmsg_len <= (len))
#define NLMSG_PAYLOAD(nlh,len) ((nlh)->nlmsg_len - NLMSG_SPACE((len)))

#define NLMSG_NOOP              0x1     /* Nothing.             */
#define NLMSG_ERROR             0x2     /* Error                */
#define NLMSG_DONE              0x3     /* End of a dump        */
#define NLMSG_OVERRUN           0x4     /* Data lost            */


static inline int nlmsg_msg_size(int payload) {
	return NLMSG_HDRLEN + payload;
}

static inline void *nlmsg_data(const struct nlmsghdr *nlh) {
	return (unsigned char *) nlh + NLMSG_HDRLEN;
}


struct nlattr {
	uint16_t nla_len;
	uint16_t nla_type;
};


#define NLA_F_NESTED		(1 << 15)
#define NLA_F_NET_BYTEORDER	(1 << 14)
#define NLA_TYPE_MASK		~(NLA_F_NESTED | NLA_F_NET_BYTEORDER)

#define NLA_ALIGNTO		4
#define NLA_ALIGN(len)		(((len) + NLA_ALIGNTO - 1) & ~(NLA_ALIGNTO - 1))
#define NLA_HDRLEN		((int) NLA_ALIGN(sizeof(struct nlattr)))

static inline int nla_attr_size(int payload)
{
	return NLA_HDRLEN + payload;
}

static inline int nla_total_size(int payload)
{
	return NLA_ALIGN(nla_attr_size(payload));
}

static inline void *nla_data(const struct nlattr *nla)
{
	return (char *) nla + NLA_HDRLEN;
}

#define LINUX_RTM_NEWLINK	16
#define LINUX_RTM_DELLINK	17
#define LINUX_RTM_GETLINK	18
#define LINUX_RTM_SETLINK	19
#define LINUX_RTM_NEWADDR	20
#define LINUX_RTM_DELADDR	21
#define LINUX_RTM_GETADDR	22
#define LINUX_RTM_NEWNEIGH	28
#define LINUX_RTM_DELNEIGH	29
#define LINUX_RTM_GETNEIGH	30

struct ifinfomsg {
	unsigned char	ifi_family;
	unsigned char	__ifi_pad;
	unsigned short	ifi_type;	/* ARPHRD_* */
	int		ifi_index;	/* Link index	*/
	unsigned	ifi_flags;	/* IFF_* flags	*/
	unsigned	ifi_change;	/* IFF_* change mask */
};

#define IFLA_UNSPEC 0
#define IFLA_ADDRESS 1
#define IFLA_BROADCAST 2
#define IFLA_IFNAME 3
#define IFLA_MTU 4
#define IFLA_LINK 5

struct ifaddrmsg {
	uint8_t       ifa_family;
	uint8_t       ifa_prefixlen;	/* The prefix length	*/
	uint8_t       ifa_flags;	/* Flags		*/
	uint8_t       ifa_scope;	/* Address scope	*/
	uint32_t      ifa_index;	/* Link index		*/
};

#define IFA_UNSPEC 0
#define IFA_ADDRESS 1
#define IFA_LOCAL 2
#define IFA_LABEL 3
#define IFA_BROADCAST 4
#define IFA_ANYCAST 5
#define IFA_CACHEINFO 6
#define IFA_MULTICAST 7
#define IFA_FLAGS 8

/* ifa_flags */
#define IFA_F_SECONDARY		0x01
#define IFA_F_TEMPORARY		IFA_F_SECONDARY
#define	IFA_F_NODAD		0x02
#define IFA_F_OPTIMISTIC	0x04
#define IFA_F_DADFAILED		0x08
#define	IFA_F_HOMEADDRESS	0x10
#define IFA_F_DEPRECATED	0x20
#define IFA_F_TENTATIVE		0x40
#define IFA_F_PERMANENT		0x80
#define IFA_F_MANAGETEMPADDR	0x100
#define IFA_F_NOPREFIXROUTE	0x200
#define IFA_F_MCAUTOJOIN	0x400
#define IFA_F_STABLE_PRIVACY	0x800

struct ndmsg {
	uint8_t		ndm_family;
	uint8_t		ndm_pad1;
	uint16_t	ndm_pad2;
	int32_t		ndm_ifindex;
	uint16_t	ndm_state;
	uint8_t		ndm_flags;
	uint8_t		ndm_type;
};

#define NDA_UNSPEC		0x0
#define NDA_DST			0x01
#define NDA_LLADDR		0x02
#define NDA_CACHEINFO		0x03

#define NTF_USE			0x01
#define NTF_SELF		0x02
#define NTF_MASTER		0x04
#define NTF_PROXY		0x08
#define NTF_EXT_LEARNED		0x10
#define NTF_OFFLOADED		0x20
#define NTF_ROUTER		0x80

#define NUD_INCOMPLETE	0x01
#define NUD_REACHABLE	0x02
#define NUD_STALE	0x04
#define NUD_DELAY	0x08
#define NUD_PROBE	0x10
#define NUD_FAILED	0x20

/* Domain ID for supporting NETLINK socket on FreeBSD (actually 16 on Linux) */
#define AF_NETLINK		AF_VENDOR00
#define PF_NETLINK		AF_NETLINK

__BEGIN_DECLS
void netlink_init(void);
__END_DECLS

#endif /* _NETLINK_H_ */
