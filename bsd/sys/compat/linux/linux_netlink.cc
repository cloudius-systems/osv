/*
 * Linux NETLINK socket implementation.
 *
 * NETLINK is used to support IPv4/IPv6 LIBC getifaddrs(), if_nameindex().
 *
 * Warning: Tx/Rx messages are compatible with Linux not FreeBSD.
 */

#include <osv/initialize.hh>
#include <bsd/porting/netport.h>

#include <bsd/sys/sys/param.h>
#include <bsd/sys/sys/domain.h>
#include <bsd/sys/sys/mbuf.h>
#include <bsd/sys/sys/priv.h>
#include <bsd/sys/sys/protosw.h>
#include <bsd/sys/sys/socket.h>
#include <bsd/sys/sys/socketvar.h>
#include <bsd/sys/sys/sysctl.h>

#include <bsd/sys/net/if.h>
#include <bsd/sys/net/if_dl.h>
#include <bsd/sys/net/if_llatbl.h>
#include <bsd/sys/net/if_types.h>
#include <bsd/sys/net/netisr.h>
#include <bsd/sys/net/raw_cb.h>
#include <bsd/sys/net/route.h>
#include <bsd/sys/net/vnet.h>

#include <bsd/sys/netinet/in.h>
#include <bsd/sys/netinet/if_ether.h>
#include <bsd/sys/net/if_llatbl.h>

#ifdef INET6
#include <bsd/sys/netinet/ip6.h>
#include <bsd/sys/netinet6/ip6_var.h>
#include <bsd/sys/netinet6/in6_var.h>
#include <bsd/sys/netinet6/scope6_var.h>
#include <bsd/sys/netinet6/nd6.h>
#endif

#include <bsd/sys/compat/linux/linux.h>
#include <bsd/sys/compat/linux/linux_netlink.h>
#include <bsd/sys/compat/linux/linux_socket.h>

#if !defined(offsetof)
#define offsetof(TYPE, MEMBER) __builtin_offsetof (TYPE, MEMBER)
#endif

mutex netlink_mtx;

#define NETLINK_LOCK()	 mutex_lock(&netlink_mtx)
#define NETLINK_UNLOCK() mutex_unlock(&netlink_mtx)
#define NETLINK_LOCK_ASSERT()	 assert(netlink_mtx.owned())

struct bsd_sockaddr_nl {
	uint8_t		nl_len;       /* length of this struct */
	bsd_sa_family_t nl_family;    /* AF_NETLINK */
	unsigned short  nl_pad;       /* Zero */
	pid_t		nl_pid;       /* Port ID */
	uint32_t	nl_groups;    /* Multicast groups mask */
};

struct netlinkcb {
	struct rawcb	raw;
	pid_t		nl_pid;
};

std::atomic<pid_t> _nl_next_gen_pid(2);


MALLOC_DEFINE(M_NETLINK, "netlink", "netlink socket");

static struct	bsd_sockaddr netlink_src = { 2, PF_NETLINK, };



static size_t mask_to_prefix_len(const uint8_t *bytes, size_t n_bytes)
{
	for (size_t i=0; i <n_bytes; ++i) {
		uint8_t val = bytes[n_bytes - i - 1];
		if (val == 0)
			continue;
		/* Find first bit in byte which is set */
		int bit_pos = __builtin_ffs((long)val) - 1;
		size_t pos = 8 * (n_bytes - i) - bit_pos;
		return pos;
	}
	return 0;
}

static int get_sockaddr_mask_prefix_len(struct bsd_sockaddr *sa)
{
	void *data;
	int	  data_len;

	if (!sa)
		return 0;

	switch (sa->sa_family) {
#ifdef INET
	case AF_INET:
		data = &((struct bsd_sockaddr_in *)sa)->sin_addr;
		data_len = sizeof(((struct bsd_sockaddr_in *)sa)->sin_addr);
		break;
#endif
#ifdef INET6
	case AF_INET6:
		data = ((struct bsd_sockaddr_in6 *)sa)->sin6_addr.s6_addr;
		data_len = sizeof(((struct bsd_sockaddr_in6 *)sa)->sin6_addr);
		break;
#endif
	default:
		return 0;
	}

	return mask_to_prefix_len((uint8_t *)data, data_len);
}


void *nl_m_put(struct mbuf *m0, int len)
{
	struct mbuf *m, *n;
	void *data = NULL;
	int space;

	/* Skip to last buffer in chain */
	for (m = m0; m->m_hdr.mh_next != NULL; m = m->m_hdr.mh_next)
		;

	space = M_TRAILINGSPACE(m);
	if (len <= space) {
		/* Add to existing buffer if there is space */
		data = m->m_hdr.mh_data + m->m_hdr.mh_len;
		m->m_hdr.mh_len += len;
	} else {
		/* Add additional buffer for new message */
		if (len > MLEN)
			return NULL;
		n = m_get(M_NOWAIT, m->m_hdr.mh_type);
		if (n == NULL)
			return NULL;
		data = n->m_hdr.mh_data;
		n->m_hdr.mh_len = len;
		m->m_hdr.mh_next = n;
		m = n;
	}
	if (m0->m_hdr.mh_flags & M_PKTHDR) {
		m0->M_dat.MH.MH_pkthdr.len += len;
	}
	return data;
}

struct nlmsghdr * nlmsg_put(struct mbuf *m, uint32_t pid, uint32_t seq, int type, int len, int flags)
{
	struct nlmsghdr *nlh;
	int size = nlmsg_msg_size(len);
	int align_size = NLMSG_ALIGN(size);
	nlh = (struct nlmsghdr *) nl_m_put(m, align_size);
	if (!nlh)
		return NULL;
	nlh->nlmsg_type = type;
	nlh->nlmsg_len = size;
	nlh->nlmsg_flags = flags;
	nlh->nlmsg_pid = pid;
	nlh->nlmsg_seq = seq;
	if (align_size != size) {
		memset(nlmsg_data(nlh) + len, 0, align_size - size);
	}
	return nlh;
}

struct nlmsghdr * nlmsg_begin(struct mbuf *m, uint32_t pid, uint32_t seq, int type, int len, int flags)
{
	return nlmsg_put(m, pid, seq, type, len, flags);
}

void nlmsg_end(struct mbuf *m, struct nlmsghdr *nlh)
{
	nlh->nlmsg_len = m->M_dat.MH.MH_pkthdr.len - ((uintptr_t)nlh - (uintptr_t)m->m_hdr.mh_data);
}

int nla_put(struct mbuf *m, int attrtype, int len, const void *src)
{
	struct nlattr *nla;
	int size = nla_attr_size(len);
	int align_size = NLA_ALIGN(size);
	nla = (struct nlattr *)nl_m_put(m, align_size);
	if (!nla)
		return ENOMEM;
	nla->nla_len = size;
	nla->nla_type = attrtype;
	void *dest = nla_data(nla);
	memcpy(dest, src, len);
	if (size != align_size)
		memset(dest + size, 0, (align_size - size));
	return 0;
}

template<class T>
int nla_put_type(struct mbuf *m, int attrtype, T val)
{
	return nla_put(m, attrtype, sizeof(val), &val);
}

int nla_put_string(struct mbuf *m, int attrtype, const char *str)
{
	return nla_put(m, attrtype, strlen(str) + 1, str);
}

int nla_put_sockaddr(struct mbuf *m, int attrtype, struct bsd_sockaddr *sa)
{
	void *data;
	int data_len;

	if (!sa)
		return 0;

	switch (sa->sa_family) {
#ifdef INET
	case AF_INET:
		data = &((struct bsd_sockaddr_in *)sa)->sin_addr;
		data_len = sizeof(((struct bsd_sockaddr_in *)sa)->sin_addr);
		break;
#endif
#ifdef INET6
	case AF_INET6:
		data = ((struct bsd_sockaddr_in6 *)sa)->sin6_addr.s6_addr;
		data_len = sizeof(((struct bsd_sockaddr_in6 *)sa)->sin6_addr);
		break;
#endif
	case AF_LINK:
		data = ((struct bsd_sockaddr_dl *)sa)->sdl_data + ((struct bsd_sockaddr_dl *)sa)->sdl_nlen;
		data_len = ((struct bsd_sockaddr_dl *)sa)->sdl_alen;
		break;
	default:
		data = sa->sa_data;
		data_len = sa->sa_len;
		break;
	}

	return nla_put(m, attrtype, data_len, data);
}

static int	netlink_output(struct mbuf *m, struct socket *so);


/* Currently messages are always redirected back to the socket which
 * sent the message, so an ISR dispatch handler is not needed.
 *
 */

static void	netlink_input(struct mbuf *m);

static struct netisr_handler netlink_nh = initialize_with([] (netisr_handler& x) {
	x.nh_name = "netlink";
	x.nh_handler = netlink_input;
	x.nh_proto = NETISR_NETLINK;
	x.nh_policy = NETISR_POLICY_SOURCE;
});

static int
raw_input_netlink_cb(struct mbuf *m, struct sockproto *proto, struct bsd_sockaddr *src, struct rawcb *rp)
{
	int fibnum;

	KASSERT(m != NULL, ("%s: m is NULL", __func__));
	KASSERT(proto != NULL, ("%s: proto is NULL", __func__));
	KASSERT(rp != NULL, ("%s: rp is NULL", __func__));

	/* Check if it is a rts and the fib matches the one of the socket. */
	fibnum = M_GETFIB(m);
	if (proto->sp_family != PF_NETLINK ||
		rp->rcb_socket == NULL ||
		rp->rcb_socket->so_fibnum == fibnum)
		return (0);

	/* Filtering requested and no match, the socket shall be skipped. */
	return (1);
}

static void
netlink_input(struct mbuf *m)
{
	struct sockproto netlink_proto;

	netlink_proto.sp_family = PF_NETLINK;

	raw_input_ext(m, &netlink_proto, &netlink_src, raw_input_netlink_cb);
}

void
netlink_init(void)
{
	mutex_init(&netlink_mtx);
	netisr_register(&netlink_nh);
}

SYSINIT(netlink, SI_SUB_PROTO_DOMAIN, SI_ORDER_THIRD, netlink_init, 0);

/*
 * It really doesn't make any sense at all for this code to share much
 * with raw_usrreq.c, since its functionality is so restricted.	 XXX
 */
static void
netlink_abort(struct socket *so)
{
	raw_usrreqs.pru_abort(so);
}

static void
netlink_close(struct socket *so)
{
	raw_usrreqs.pru_close(so);
}

/* pru_accept is EOPNOTSUPP */

static int
netlink_attach(struct socket *so, int proto, struct thread *td)
{
	struct netlinkcb *ncb;
	struct rawcb *rp;
	int s, error;

	KASSERT(so->so_pcb == NULL, ("netlink_attach: so_pcb != NULL"));

	/* XXX */
	ncb = (netlinkcb *)malloc(sizeof *ncb);
	if (ncb == NULL)
		return ENOBUFS;
	bzero(ncb, sizeof *ncb);
	rp = &ncb->raw;

	/*
	 * The splnet() is necessary to block protocols from sending
	 * error notifications (like RTM_REDIRECT or RTM_LOSING) while
	 * this PCB is extant but incompletely initialized.
	 * Probably we should try to do more of this work beforehand and
	 * eliminate the spl.
	 */
	s = splnet();
	so->so_pcb = (caddr_t)rp;
	so->set_mutex(&netlink_mtx);
	so->so_fibnum = 0;
	error = raw_attach(so, proto);
	rp = sotorawcb(so);
	if (error) {
		splx(s);
		so->so_pcb = NULL;
		free(rp);
		return error;
	}
	NETLINK_LOCK();
	soisconnected(so);
	NETLINK_UNLOCK();
	so->so_options |= SO_USELOOPBACK;
	splx(s);
	return 0;
}

static int
netlink_bind(struct socket *so, struct bsd_sockaddr *nam, struct thread *td)
{
	struct rawcb *rp = sotorawcb(so);

	KASSERT(rp != NULL, ("netlink_bind: rp == NULL"));

	if (nam->sa_family == AF_NETLINK) {
		if (nam->sa_len != sizeof(struct bsd_sockaddr_nl)) {
			bsd_log(ERR, "%s(%d) %s Invalid sockaddr_nl length %d expected %d\n",
				__FILE__, __LINE__, __FUNCTION__, nam->sa_len, sizeof(struct bsd_sockaddr_nl));
			return EINVAL;
		}
		auto *ncb = reinterpret_cast<netlinkcb*>(rp);
		bsd_sockaddr_nl *nl_sock_addr = (bsd_sockaddr_nl*)nam;
		if (nl_sock_addr->nl_pid == 0) { // kernel needs to assign pid
			auto assigned_pid = _nl_next_gen_pid.fetch_add(1, std::memory_order_relaxed);
			ncb->nl_pid = assigned_pid;
		} else {
			ncb->nl_pid = nl_sock_addr->nl_pid;
		}
		return 0;
	}
	return (raw_usrreqs.pru_bind(so, nam, td)); /* xxx just EINVAL */
}

static int
netlink_connect(struct socket *so, struct bsd_sockaddr *nam, struct thread *td)
{
	return (raw_usrreqs.pru_connect(so, nam, td)); /* XXX just EINVAL */
}

/* pru_connect2 is EOPNOTSUPP */
/* pru_control is EOPNOTSUPP */

static void
netlink_detach(struct socket *so)
{
	struct rawcb *rp = sotorawcb(so);

	KASSERT(rp != NULL, ("netlink_detach: rp == NULL"));

	raw_usrreqs.pru_detach(so);
}

static int
netlink_disconnect(struct socket *so)
{
	return (raw_usrreqs.pru_disconnect(so));
}

/* pru_listen is EOPNOTSUPP */

static int
netlink_peeraddr(struct socket *so, struct bsd_sockaddr **nam)
{
	return (raw_usrreqs.pru_peeraddr(so, nam));
}

/* pru_rcvd is EOPNOTSUPP */
/* pru_rcvoob is EOPNOTSUPP */

static int
netlink_send(struct socket *so, int flags, struct mbuf *m, struct bsd_sockaddr *nam,
	 struct mbuf *control, struct thread *td)
{
	return (raw_usrreqs.pru_send(so, flags, m, nam, control, td));
}

/* pru_sense is null */

static int
netlink_shutdown(struct socket *so)
{
	return (raw_usrreqs.pru_shutdown(so));
}

static int
netlink_sockaddr(struct socket *so, struct bsd_sockaddr **nam)
{
	return (raw_usrreqs.pru_sockaddr(so, nam));
}

static struct pr_usrreqs netlink_usrreqs = initialize_with([] (pr_usrreqs& x) {
	x.pru_abort =		netlink_abort;
	x.pru_attach =		netlink_attach;
	x.pru_bind =		netlink_bind;
	x.pru_connect =		netlink_connect;
	x.pru_detach =		netlink_detach;
	x.pru_disconnect =	netlink_disconnect;
	x.pru_peeraddr =	netlink_peeraddr;
	x.pru_send =		netlink_send;
	x.pru_shutdown =	netlink_shutdown;
	x.pru_sockaddr =	netlink_sockaddr;
	x.pru_close =		netlink_close;
});

static void netlink_dispatch(struct socket *so __bsd_unused2, struct mbuf *m)
{
	netisr_queue(NETISR_NETLINK, m);
}

static int
netlink_senderr(struct socket *so, struct nlmsghdr *nlm, int error)
{
	struct mbuf *m;
	struct nlmsghdr *hdr;
	struct nlmsgerr *err;

	m = m_getcl(M_DONTWAIT, MT_DATA, M_PKTHDR);
	if (!m) {
		return ENOBUFS;
	}

	if ((hdr = (struct nlmsghdr *)nlmsg_put(m,
						nlm ? nlm->nlmsg_pid : 0,
						nlm ? nlm->nlmsg_seq : 0,
						NLMSG_ERROR, sizeof(*err),
						nlm ? nlm->nlmsg_flags : 0)) == NULL) {
		m_freem(m);
		return ENOBUFS;
	}
	err = (struct nlmsgerr *) nlmsg_data(hdr);
	err->error = error;
	if (nlm) {
		err->msg = *nlm;
	} else {
		memset(&err->msg, 0, sizeof(err->msg));
		nlm = &err->msg;
	}

	netlink_dispatch(so, m);
	return 0;
}

static int
netlink_process_getlink_msg(struct socket *so, struct nlmsghdr *nlm)
{
	struct ifnet *ifp = NULL;
	struct bsd_ifaddr *ifa;
	struct nlmsghdr *nlh;
	struct ifinfomsg *ifm;
	struct mbuf *m = NULL;
	int error = 0;

	m = m_getcl(M_DONTWAIT, MT_DATA, M_PKTHDR);
	if (!m) {
		return ENOBUFS;
	}

	IFNET_RLOCK();
	TAILQ_FOREACH(ifp, &V_ifnet, if_link) {
		IF_ADDR_RLOCK(ifp);

		nlh = nlmsg_begin(m, nlm->nlmsg_pid, nlm->nlmsg_seq, LINUX_RTM_NEWLINK, sizeof(*ifm), nlm->nlmsg_flags);
		if (!nlh) {
			error = ENOBUFS;
			goto done;
		}

		ifm = (struct ifinfomsg *) nlmsg_data(nlh);
		ifm->ifi_family = AF_UNSPEC;
		ifm->__ifi_pad = 0;
		ifm->ifi_type = ifp->if_data.ifi_type;
		ifm->ifi_index = ifp->if_index;
		ifm->ifi_flags = ifp->if_flags | ifp->if_drv_flags;
		ifm->ifi_change = 0;
		if (nla_put_string(m, IFLA_IFNAME, ifp->if_xname) ||
			nla_put_type<uint32_t>(m, IFLA_LINK, ifp->if_index)) {
			error = ENOBUFS;
			goto done;
		}
		/* Add hw address info */
		for (ifa = ifp->if_addr; ifa != NULL; ifa = TAILQ_NEXT(ifa, ifa_link)) {
			if (ifa->ifa_addr->sa_family == AF_LINK)
				break;
		}
		if (ifa) {
			if (nla_put_sockaddr(m, IFLA_ADDRESS, ifa->ifa_addr) ||
				nla_put_sockaddr(m, IFLA_BROADCAST, ifa->ifa_broadaddr)){
				error = ENOBUFS;
				goto done;
			}
		}

		IF_ADDR_RUNLOCK(ifp);
		nlmsg_end(m, nlh);
	}
	nlh = nlmsg_put(m, nlm->nlmsg_pid, nlm->nlmsg_seq, NLMSG_DONE, 0, nlm->nlmsg_flags);

done:
	if (ifp != NULL)
		IF_ADDR_RUNLOCK(ifp);
	IFNET_RUNLOCK();
	if (m) {
		if (!error) {
			netlink_dispatch(so, m);
		} else {
			m_freem(m);
		}
	}
	return (error);
}

static int
netlink_process_getaddr_msg(struct socket *so, struct nlmsghdr *nlm)
{
	struct ifnet *ifp = NULL;
	struct bsd_ifaddr *ifa;
	struct nlmsghdr *nlh;
	struct ifaddrmsg *ifm;
	struct mbuf *m = NULL;
	int error = 0;

	m = m_getcl(M_DONTWAIT, MT_DATA, M_PKTHDR);
	if (!m) {
		return ENOBUFS;
	}

	IFNET_RLOCK();
	TAILQ_FOREACH(ifp, &V_ifnet, if_link) {
		IF_ADDR_RLOCK(ifp);
		ifa = ifp->if_addr;
		for (ifa = ifp->if_addr; ifa != NULL; ifa = TAILQ_NEXT(ifa, ifa_link)) {
			int af = ifa->ifa_addr->sa_family;

			switch (af) {
#ifdef INET
			case AF_INET:
				af = LINUX_AF_INET;
				break;
#endif
#ifdef INET6
			case AF_INET6:
				af = LINUX_AF_INET6;
				break;
#endif
			default:
				af = -1;
			}
			if (af < 0)
				continue;

			if (!ifa->ifa_addr)
				continue;

			nlh = nlmsg_begin(m, nlm->nlmsg_pid, nlm->nlmsg_seq, LINUX_RTM_NEWADDR, sizeof(*ifm), nlm->nlmsg_flags);
			if (!nlh) {
				error = ENOBUFS;
				goto done;
			}
			ifm = (struct ifaddrmsg *) nlmsg_data(nlh);
			ifm->ifa_index = ifp->if_index;
			ifm->ifa_family = af;
			ifm->ifa_prefixlen = get_sockaddr_mask_prefix_len(ifa->ifa_netmask);
			ifm->ifa_flags = ifp->if_flags | ifp->if_drv_flags;
			ifm->ifa_scope = 0; // FIXME:
#ifdef INET6
			if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET6){
				// FreeBSD embeds the IPv6 scope ID in the IPv6 address
				// so need to extract and clear it before returning it.
				struct bsd_sockaddr_in6 addr, broadaddr;
				struct bsd_sockaddr *p_addr = ifa->ifa_addr, *p_broadaddr = ifa->ifa_broadaddr;
				if (p_addr && IN6_IS_ADDR_LINKLOCAL(&((struct bsd_sockaddr_in6 *)p_addr)->sin6_addr)){
					addr = *(struct bsd_sockaddr_in6 *)p_addr;
					ifm->ifa_scope = in6_getscope(&addr.sin6_addr);
					in6_clearscope(&addr.sin6_addr);
					p_addr = (struct bsd_sockaddr *)&addr;
				}
				if (p_broadaddr && IN6_IS_ADDR_LINKLOCAL(&((struct bsd_sockaddr_in6 *)p_broadaddr)->sin6_addr)){
					broadaddr = *(struct bsd_sockaddr_in6 *)p_broadaddr;
					in6_clearscope(&broadaddr.sin6_addr);
					p_broadaddr = (struct bsd_sockaddr *)&broadaddr;
				}
				if (nla_put_sockaddr(m, IFA_ADDRESS, p_addr)){
					error = ENOBUFS;
					goto done;
				}
				if (!(ifm->ifa_flags & IFF_LOOPBACK) && nla_put_sockaddr(m, IFA_BROADCAST, p_broadaddr)){
					error = ENOBUFS;
					goto done;
				}
			}
			else
#endif
			{
				if (nla_put_sockaddr(m, IFA_ADDRESS, ifa->ifa_addr)){
					error = ENOBUFS;
					goto done;
				}
				if (!(ifm->ifa_flags & IFF_LOOPBACK) && nla_put_sockaddr(m, IFA_BROADCAST, ifa->ifa_broadaddr)){
					error = ENOBUFS;
					goto done;
				}
			}
			if (nla_put_string(m, IFA_LABEL, ifp->if_xname)) {
				error = ENOBUFS;
				goto done;
			}
			nlmsg_end(m, nlh);
		}

		IF_ADDR_RUNLOCK(ifp);
	}
	nlh = nlmsg_put(m, nlm->nlmsg_pid, nlm->nlmsg_seq, NLMSG_DONE, 0, nlm->nlmsg_flags);
done:
	if (ifp != NULL)
		IF_ADDR_RUNLOCK(ifp);
	IFNET_RUNLOCK();
	if (m) {
		if (!error) {
			netlink_dispatch(so, m);
		} else {
			m_freem(m);
		}
	}
	return (error);
}

static uint16_t lle_state_to_ndm_state(int family, int state)
{
#ifdef INET6
	if (family == AF_INET6) {
		switch(state) {
		case ND6_LLINFO_INCOMPLETE:
			return NUD_INCOMPLETE;
		case ND6_LLINFO_REACHABLE:
			return NUD_REACHABLE;
		case ND6_LLINFO_STALE:
			return NUD_STALE;
		case ND6_LLINFO_DELAY:
			return NUD_DELAY;
		case ND6_LLINFO_PROBE:
			return NUD_PROBE;
		case ND6_LLINFO_NOSTATE:
		default:
			return 0;
		}
	}
#endif
	if (family == AF_INET) {
		return NUD_REACHABLE;
	}

	return 0;
}

static int netlink_bsd_to_linux_family(int family)
{
	switch(family) {
	case AF_INET:
		return LINUX_AF_INET;
#ifdef INET6
	case AF_INET6:
		return LINUX_AF_INET6;
#endif
	default:
		return -1;
	}
}

struct netlink_getneigh_lle_cbdata {
	struct nlmsghdr *nlm;
	struct mbuf *m;
	uint16_t family;
	uint16_t state;
};

static int
netlink_getneigh_lle_cb(struct lltable *llt, struct llentry *lle, void *data)
{
	struct netlink_getneigh_lle_cbdata *cbdata = (struct netlink_getneigh_lle_cbdata *) data;
	int ndm_family = netlink_bsd_to_linux_family(llt->llt_af);
	int ndm_state = lle_state_to_ndm_state(llt->llt_af, lle->ln_state);

	if (cbdata->family && cbdata->family != ndm_family)
		return 0;

	if (cbdata->state && !(cbdata->state & ndm_state))
		return 0;

	struct nlmsghdr *nlm = cbdata->nlm;
	struct mbuf *m = cbdata->m;
	struct ndmsg *ndm;
	struct nlmsghdr *nlh = nlmsg_begin(m, nlm->nlmsg_pid, nlm->nlmsg_seq, LINUX_RTM_NEWNEIGH, sizeof(*ndm), nlm->nlmsg_flags);

	if (!nlh) {
		return ENOBUFS;
	}

	ndm = (struct ndmsg *) nlmsg_data(nlh);
	ndm->ndm_family = ndm_family;
	ndm->ndm_ifindex = llt->llt_ifp->if_index;
	ndm->ndm_state = ndm_state;
	ndm->ndm_flags = 0;
	if (lle->ln_router)
		ndm->ndm_flags |= NTF_ROUTER;
	ndm->ndm_type = 0;

	struct bsd_sockaddr *sa = L3_ADDR(lle);
	if (sa->sa_family == AF_INET) {
		struct bsd_sockaddr_in *sa4 = (struct bsd_sockaddr_in *) sa;
		if (nla_put_type(m, NDA_DST, sa4->sin_addr)) {
			return ENOBUFS;
		}
	}
#ifdef INET6
	else if (sa->sa_family == AF_INET6) {
		struct bsd_sockaddr_in6 sa6 = *(struct bsd_sockaddr_in6 *) sa;
		if (IN6_IS_ADDR_LINKLOCAL(&sa6.sin6_addr)){
			in6_clearscope(&sa6.sin6_addr);
		}
		if (nla_put_type(m, NDA_DST, sa6.sin6_addr)) {
			return ENOBUFS;
		}
	}
#endif

	if (nla_put(m, NDA_LLADDR, 6, lle->ll_addr.mac16)) {
		return ENOBUFS;
	}

	nlmsg_end(m, nlh);

	return 0;
}


static int
netlink_getneigh_lltable_cb(struct lltable *llt, void *cbdata)
{
	struct netlink_getneigh_lle_cbdata *data = (struct netlink_getneigh_lle_cbdata *) cbdata;
	int error = 0;

	if (data->family && data->family != netlink_bsd_to_linux_family(llt->llt_af))
		return 0;
	if (llt->llt_ifp->if_flags & IFF_LOOPBACK)
		return 0;

	IF_AFDATA_RLOCK(llt->llt_ifp);
	error = lltable_foreach_lle(llt, netlink_getneigh_lle_cb, data);
	IF_AFDATA_RUNLOCK(llt->llt_ifp);

	return error;
}

static int
netlink_process_getneigh_msg(struct socket *so, struct nlmsghdr *nlm)
{
	struct mbuf *m = NULL;
	struct nlmsghdr *nlh;
	struct netlink_getneigh_lle_cbdata cbdata;
	int error;

	if (nlm->nlmsg_len < sizeof (struct ndmsg)) {
		return EINVAL;
	}

	m = m_getcl(M_DONTWAIT, MT_DATA, M_PKTHDR);
	if (!m) {
		return ENOBUFS;
	}

	struct ndmsg *ndm = (struct ndmsg *) nlmsg_data(nlm);

	cbdata.nlm = nlm;
	cbdata.m = m;
	cbdata.family = ndm->ndm_family;
	cbdata.state = ndm->ndm_state;

	error = lltable_foreach(netlink_getneigh_lltable_cb, &cbdata);

	if (!error) {
		nlh = nlmsg_put(m, nlm->nlmsg_pid, nlm->nlmsg_seq, NLMSG_DONE, 0, nlm->nlmsg_flags);
		netlink_dispatch(so, m);
	} else {
		m_free(m);
	}

	return error;
}

static int
netlink_process_msg(struct mbuf *m, struct socket *so)
{
	struct nlmsghdr *nlm = NULL;
	int len, error = 0;

#define senderr(e) { error = e; goto flush;}
	if (m == NULL || (m->m_hdr.mh_flags & M_PKTHDR) == 0)
		panic("Invalid message");
	len = m->M_dat.MH.MH_pkthdr.len;
	if (len < sizeof(struct nlmsghdr))
		senderr(EINVAL);
	if ((m = m_pullup(m, len)) == NULL)
		senderr(ENOBUFS);
	if (len != mtod(m, struct nlmsghdr *)->nlmsg_len)
		senderr(EINVAL);
	nlm = mtod(m, struct nlmsghdr *);

	switch(nlm->nlmsg_type) {
		case LINUX_RTM_GETLINK:
			error = netlink_process_getlink_msg(so, nlm);
			break;
		case LINUX_RTM_GETADDR:
			error = netlink_process_getaddr_msg(so, nlm);
			break;
		case LINUX_RTM_GETNEIGH:
			error = netlink_process_getneigh_msg(so, nlm);
			break;
		default:
			senderr(EOPNOTSUPP);
	}

flush:
	if (error) {
		netlink_senderr(so, nlm, error);
	}
	if (m) {
		m_freem(m);
	}

	return (error);
}

static int
netlink_output(struct mbuf *m, struct socket *so)
{
	return netlink_process_msg(m, so);
}

/*
 * Definitions of protocols supported in the NETLINK domain.
 */

extern struct domain netlinkdomain;		/* or at least forward */

static struct protosw netlinksw[] = {
	initialize_with([] (protosw& x) {
	x.pr_type =		SOCK_RAW;
	x.pr_domain =		&netlinkdomain;
	x.pr_flags =		PR_ATOMIC|PR_ADDR;
	x.pr_output =		netlink_output;
	x.pr_ctlinput =		raw_ctlinput;
	x.pr_init =		raw_init;
	x.pr_usrreqs =		&netlink_usrreqs;
	}),
	initialize_with([] (protosw& x) {
	x.pr_type =		SOCK_DGRAM;
	x.pr_domain =		&netlinkdomain;
	x.pr_flags =		PR_ATOMIC|PR_ADDR;
	x.pr_output =		netlink_output;
	x.pr_ctlinput =		raw_ctlinput;
	x.pr_init =		raw_init;
	x.pr_usrreqs =		&netlink_usrreqs;
	}),
};

struct domain netlinkdomain = initialize_with([] (domain& x) {
	x.dom_family =		PF_NETLINK;
	x.dom_name =		"netlink";
	x.dom_protosw =		netlinksw;
	x.dom_protoswNPROTOSW =	&netlinksw[sizeof(netlinksw)/sizeof(netlinksw[0])];
});

VNET_DOMAIN_SET(netlink);
