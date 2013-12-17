/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */


#include <sys/cdefs.h>

#include "drivers/virtio.hh"
#include "drivers/virtio-net.hh"
#include "drivers/pci-device.hh"
#include "interrupt.hh"

#include "mempool.hh"
#include "mmu.hh"

#include <sstream>
#include <string>
#include <string.h>
#include <map>
#include <errno.h>
#include <osv/debug.h>

#include "sched.hh"
#include "osv/trace.hh"

#include "drivers/clock.hh"
#include "drivers/clockevent.hh"

#include <osv/device.h>
#include <osv/ioctl.h>
#include <bsd/sys/net/ethernet.h>
#include <bsd/sys/net/if_types.h>
#include <bsd/sys/sys/param.h>

#include <bsd/sys/net/ethernet.h>
#include <bsd/sys/net/if_vlan_var.h>
#include <bsd/sys/netinet/in.h>
#include <bsd/sys/netinet/ip.h>
#include <bsd/sys/netinet/udp.h>
#include <bsd/sys/netinet/tcp.h>

TRACEPOINT(trace_virtio_net_rx_packet, "if=%d, len=%d", int, int);
TRACEPOINT(trace_virtio_net_rx_wake, "");
TRACEPOINT(trace_virtio_net_fill_rx_ring, "if=%d", int);
TRACEPOINT(trace_virtio_net_fill_rx_ring_added, "if=%d, added=%d", int, int);
TRACEPOINT(trace_virtio_net_tx_packet, "if=%d, len=%d", int, int);
TRACEPOINT(trace_virtio_net_tx_failed_add_buf, "if=%d", int);
TRACEPOINT(trace_virtio_net_tx_no_space_calling_gc, "if=%d", int);
using namespace memory;

// TODO list
// irq thread affinity and tx affinity
// tx zero copy
// vlans?

namespace virtio {

    int virtio_net::_instance = 0;

    #define virtio_net_tag "virtio-net"
    #define virtio_net_d(...)   tprintf_d(virtio_net_tag, __VA_ARGS__)
    #define virtio_net_i(...)   tprintf_i(virtio_net_tag, __VA_ARGS__)
    #define virtio_net_w(...)   tprintf_w(virtio_net_tag, __VA_ARGS__)
    #define virtio_net_e(...)   tprintf_e(virtio_net_tag, __VA_ARGS__)

    static int virtio_if_ioctl(
            struct ifnet *ifp,
            u_long command,
            caddr_t data)
    {
        virtio_net_d("virtio_if_ioctl %x", command);

        int error = 0;
        switch(command) {
        case SIOCSIFMTU:
            virtio_net_d("SIOCSIFMTU");
            break;
        case SIOCSIFFLAGS:
            virtio_net_d("SIOCSIFFLAGS");
            /* Change status ifup, ifdown */
            if (ifp->if_flags & IFF_UP) {
                ifp->if_drv_flags |= IFF_DRV_RUNNING;
                virtio_net_d("if_up");
            } else {
                ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
                virtio_net_d("if_down");
            }
            break;
        case SIOCADDMULTI:
        case SIOCDELMULTI:
            virtio_net_d("SIOCDELMULTI");
            break;
        default:
            virtio_net_d("redirecting to ether_ioctl()...");
            error = ether_ioctl(ifp, command, data);
            break;
        }

        return(error);
    }

    /**
     * Invalidate the local Tx queues.
     * @param ifp upper layer instance handle
     */
    static void virtio_if_qflush(struct ifnet *ifp)
    {
        /*
         * Since virtio_net currently doesn't have any Tx queue we just
         * flush the upper layer queues.
         */
        if_qflush(ifp);
    }

    /**
     * Transmits a single mbuf instance.
     * @param ifp upper layer instance handle
     * @param m_head mbuf to transmit
     *
     * @return 0 in case of success and an appropriate error code
     *         otherwise
     */
    static int virtio_if_transmit(struct ifnet* ifp, struct mbuf* m_head)
    {
        virtio_net* vnet = (virtio_net*)ifp->if_softc;

        virtio_net_d("%s_start", __FUNCTION__);

        /* Process packets */
        vnet->_tx_ring_lock.lock();

        virtio_net_d("*** processing packet! ***");

        int error = vnet->tx_locked(m_head);

        if (!error)
            vnet->kick(1);

        vnet->_tx_ring_lock.unlock();

        return error;
    }

    static void virtio_if_init(void* xsc)
    {
        virtio_net_d("Virtio-net init");
    }

    virtio_net::virtio_net(pci::device& dev)
        : virtio_driver(dev)
    {
        _rx_queue = get_virt_queue(0);
        _tx_queue = get_virt_queue(1);

        std::stringstream ss;
        ss << "virtio-net";

        _driver_name = ss.str();
        virtio_i("VIRTIO NET INSTANCE");
        _id = _instance++;

        setup_features();
        read_config();

        _hdr_size = (_mergeable_bufs)? sizeof(virtio_net_hdr_mrg_rxbuf):sizeof(virtio_net_hdr);

        //register the 2 irq callback for the net
        sched::thread* rx = new sched::thread([this] { this->receiver(); });
        rx->start();

        //initialize the BSD interface _if
        _ifn = if_alloc(IFT_ETHER);
        if (_ifn == NULL) {
           //FIXME: need to handle this case - expand the above function not to allocate memory and
           // do it within the constructor.
           virtio_net_w("if_alloc failed!");
           return;
        }

        if_initname(_ifn, "eth", _id);
        _ifn->if_mtu = ETHERMTU;
        _ifn->if_softc = static_cast<void*>(this);
        _ifn->if_flags = IFF_BROADCAST /*| IFF_MULTICAST*/;
        _ifn->if_ioctl = virtio_if_ioctl;
        _ifn->if_transmit = virtio_if_transmit;
        _ifn->if_qflush = virtio_if_qflush;
        _ifn->if_init = virtio_if_init;
        IFQ_SET_MAXLEN(&_ifn->if_snd, _tx_queue->size());

        _ifn->if_capabilities = 0;

        if (_csum) {
            _ifn->if_capabilities |= IFCAP_TXCSUM;
            if (_host_tso4) {
                _ifn->if_capabilities |= IFCAP_TSO4;
                _ifn->if_hwassist = CSUM_TCP | CSUM_UDP | CSUM_TSO;
            }
        }

        if (_guest_csum) {
            _ifn->if_capabilities |= IFCAP_RXCSUM;
            if (_guest_tso4)
                _ifn->if_capabilities |= IFCAP_LRO;
        }

        _ifn->if_capenable = _ifn->if_capabilities;

        ether_ifattach(_ifn, _config.mac);
        _msi.easy_register({
            { 0, [&] { _rx_queue->disable_interrupts(); }, rx },
            { 1, [&] { _tx_queue->disable_interrupts(); }, nullptr }
        });

        fill_rx_ring();

        add_dev_status(VIRTIO_CONFIG_S_DRIVER_OK);
    }

    virtio_net::~virtio_net()
    {
        //TODO: In theory maintain the list of free instances and gc it
        // including the thread objects and their stack
        // Will need to clear the pending requests in the ring too

        ether_ifdetach(_ifn);
        if_free(_ifn);
    }

    bool virtio_net::read_config()
    {
        //read all of the net config  in one shot
        virtio_conf_read(virtio_pci_config_offset(), &_config, sizeof(_config));

        if (get_guest_feature_bit(VIRTIO_NET_F_MAC))
            virtio_net_i("The mac addr of the device is %x:%x:%x:%x:%x:%x",
                    (u32)_config.mac[0],
                    (u32)_config.mac[1],
                    (u32)_config.mac[2],
                    (u32)_config.mac[3],
                    (u32)_config.mac[4],
                    (u32)_config.mac[5]);

        _mergeable_bufs = get_guest_feature_bit(VIRTIO_NET_F_MRG_RXBUF);
        _status = get_guest_feature_bit(VIRTIO_NET_F_STATUS);
        _tso_ecn = get_guest_feature_bit(VIRTIO_NET_F_GUEST_ECN);
        _host_tso_ecn = get_guest_feature_bit(VIRTIO_NET_F_HOST_ECN);
        _csum = get_guest_feature_bit(VIRTIO_NET_F_CSUM);
        _guest_csum = get_guest_feature_bit(VIRTIO_NET_F_GUEST_CSUM);
        _guest_tso4 = get_guest_feature_bit(VIRTIO_NET_F_GUEST_TSO4);
        _host_tso4 = get_guest_feature_bit(VIRTIO_NET_F_HOST_TSO4);

        virtio_net_i("Features: %s=%d,%s=%d", "Status", _status, "TSO_ECN", _tso_ecn);
        virtio_net_i("Features: %s=%d,%s=%d", "Host TSO ECN", _host_tso_ecn, "CSUM", _csum);
        virtio_net_i("Features: %s=%d,%s=%d", "Guest_csum", _guest_csum, "guest tso4", _guest_tso4);
        virtio_net_i("Features: %s=%d", "host tso4", _host_tso4);

        return true;
    }

    /*
     * Original comment from FreeBSD
     * Alternative method of doing receive checksum offloading. Rather
     * than parsing the received frame down to the IP header, use the
     * csum_offset to determine which CSUM_* flags are appropriate. We
     * can get by with doing this only because the checksum offsets are
     * unique for the things we care about.
     */
    bool
    virtio_net::rx_csum(struct mbuf *m, struct virtio_net_hdr *hdr)
    {
        struct ether_header *eh;
        struct ether_vlan_header *evh;
        struct udphdr *udp;
        int csum_len;
        u16 eth_type;

        csum_len = hdr->csum_start + hdr->csum_offset;

        if (csum_len < (int)sizeof(struct ether_header) + (int)sizeof(struct ip))
            return true;
        if (m->m_hdr.mh_len < csum_len)
            return true;

        eh = mtod(m, struct ether_header *);
        eth_type = ntohs(eh->ether_type);
        if (eth_type == ETHERTYPE_VLAN) {
            evh = mtod(m, struct ether_vlan_header *);
            eth_type = ntohs(evh->evl_proto);
        }

        if (eth_type != ETHERTYPE_IP) {
            return true;
        }

        /* Use the offset to determine the appropriate CSUM_* flags. */
        switch (hdr->csum_offset) {
        case offsetof(struct udphdr, uh_sum):
            if (m->m_hdr.mh_len < hdr->csum_start + (int)sizeof(struct udphdr))
                return true;
            udp = (struct udphdr *)(mtod(m, uint8_t *) + hdr->csum_start);
            if (udp->uh_sum == 0)
                return false;

            /* FALLTHROUGH */

        case offsetof(struct tcphdr, th_sum):
            m->M_dat.MH.MH_pkthdr.csum_flags |= CSUM_DATA_VALID | CSUM_PSEUDO_HDR;
            m->M_dat.MH.MH_pkthdr.csum_data = 0xFFFF;
            break;

        default:
            return true;
        }

        return false;
    }

    void virtio_net::receiver() {

        while (1) {

            // Wait for rx queue (used elements)
            virtio_driver::wait_for_queue(_rx_queue, &vring::used_ring_not_empty);
            trace_virtio_net_rx_wake();

            u32 len;
            struct mbuf* m;
            int nbufs;
            u32 offset = _hdr_size;
            //use local header that we copy out of the mbuf since we're truncating it
            struct virtio_net_hdr_mrg_rxbuf mhdr;

            while ((m = static_cast<struct mbuf*>(_rx_queue->get_buf_elem(&len))) != nullptr) {

                // TODO: should get out of the loop
                _rx_queue->get_buf_finalize();

                if (len < _hdr_size + ETHER_HDR_LEN) {
                    _ifn->if_ierrors++;
                    m_free(m);
                    continue;
                }

                memcpy(&mhdr, mtod(m, void *), _hdr_size);

                if (!_mergeable_bufs) {
                    nbufs = 1;
                } else {
                    nbufs = mhdr.num_buffers;
                }

                m->M_dat.MH.MH_pkthdr.len = len;
                m->M_dat.MH.MH_pkthdr.rcvif = _ifn;
                m->M_dat.MH.MH_pkthdr.csum_flags = 0;
                m->m_hdr.mh_len = len;

                struct mbuf* m_head, *m_tail;
                m_tail = m_head = m;

                while (--nbufs > 0) {
                    if ((m = static_cast<struct mbuf*>(_rx_queue->get_buf_elem(&len))) == nullptr) {
                        _ifn->if_ierrors++;
                        break;
                    }

                    _rx_queue->get_buf_finalize();

                    if (m->m_hdr.mh_len < (int)len)
                        len = m->m_hdr.mh_len;

                    m->m_hdr.mh_len = len;
                    m->m_hdr.mh_flags &= ~M_PKTHDR;
                    m_head->M_dat.MH.MH_pkthdr.len += len;
                    m_tail->m_hdr.mh_next = m;
                    m_tail = m;
                }

                // skip over the virtio header bytes (offset) that aren't need for the above layer
                m_adj(m_head, offset);

                if (_ifn->if_capenable & IFCAP_RXCSUM &&
                    mhdr.hdr.flags & virtio_net_hdr::VIRTIO_NET_HDR_F_NEEDS_CSUM) {
                    rx_csum(m_head, &mhdr.hdr);
                }

                _ifn->if_ipackets++;
                (*_ifn->if_input)(_ifn, m_head);

                trace_virtio_net_rx_packet(_ifn->if_index, len);

                // The interface may have been stopped while we were
                // passing the packet up the network stack.
                if ((_ifn->if_drv_flags & IFF_DRV_RUNNING) == 0)
                    break;
            }

            if (_rx_queue->refill_ring_cond()) {
                fill_rx_ring();
            }
        }
    }

    void virtio_net::fill_rx_ring()
    {
        trace_virtio_net_fill_rx_ring(_ifn->if_index);
        int added = 0;

        while (_rx_queue->avail_ring_not_empty()) {
            struct mbuf *m = m_getjcl(M_NOWAIT, MT_DATA, M_PKTHDR, MCLBYTES);
            if (!m)
                break;

            m->m_hdr.mh_len = MCLBYTES;
            u8 *mdata = mtod(m, u8*);

            _rx_queue->_sg_vec.clear();
            _rx_queue->_sg_vec.emplace_back(mmu::virt_to_phys(mdata), m->m_hdr.mh_len, vring_desc::VRING_DESC_F_WRITE);
            if (!_rx_queue->add_buf(m)) {
                m_freem(m);
                break;
            }
            added++;
        }

        trace_virtio_net_fill_rx_ring_added(_ifn->if_index, added);

        if (added) _rx_queue->kick();
    }

    /* TODO: Does it really have to be "locked"? */
    int virtio_net::tx_locked(struct mbuf *m_head, bool flush)
    {
        DEBUG_ASSERT(_tx_ring_lock.owned(), "_tx_ring_lock is not locked!");

        struct mbuf *m;
        virtio_net_req *req = new virtio_net_req;

        req->um.reset(m_head);

        if (m_head->M_dat.MH.MH_pkthdr.csum_flags != 0) {
            m = tx_offload(m_head, &req->mhdr.hdr);
            if ((m_head = m) == nullptr) {
                delete req;
                /* The buffer is not well-formed */
                return EINVAL;
            }
        }

        _tx_queue->_sg_vec.clear();
        _tx_queue->_sg_vec.emplace_back(mmu::virt_to_phys(static_cast<void*>(&req->mhdr)), _hdr_size, vring_desc::VRING_DESC_F_READ);

        for (m = m_head; m != NULL; m = m->m_hdr.mh_next) {
            if (m->m_hdr.mh_len != 0) {
                virtio_net_d("Frag len=%d:", m->m_hdr.mh_len);
                req->mhdr.num_buffers++;
                _tx_queue->_sg_vec.emplace_back(mmu::virt_to_phys(m->m_hdr.mh_data), m->m_hdr.mh_len, vring_desc::VRING_DESC_F_READ);
            }
        }

        if (!_tx_queue->avail_ring_has_room(_tx_queue->_sg_vec.size())) {
            // can't call it, this is a get buf thing
            if (_tx_queue->used_ring_not_empty()) {
                trace_virtio_net_tx_no_space_calling_gc(_ifn->if_index);
                tx_gc();
            } else {
                virtio_net_d("%s: no room", __FUNCTION__);
                delete req;
                return ENOBUFS;
            }
        }

        if (!_tx_queue->add_buf(req)) {
            trace_virtio_net_tx_failed_add_buf(_ifn->if_index);
            delete req;
            return ENOBUFS;
        }

        trace_virtio_net_tx_packet(_ifn->if_index, _tx_queue->_sg_vec.size());

        return 0;
    }

    struct mbuf*
    virtio_net::tx_offload(struct mbuf* m, struct virtio_net_hdr* hdr)
    {
        struct ether_header *eh;
        struct ether_vlan_header *evh;
        struct ip *ip;
        struct tcphdr *tcp;
        int ip_offset;
        u16 eth_type, csum_start;
        u8 ip_proto, gso_type;

        ip_offset = sizeof(struct ether_header);
        if (m->m_hdr.mh_len < ip_offset) {
            if ((m = m_pullup(m, ip_offset)) == nullptr)
                return nullptr;
        }

        eh = mtod(m, struct ether_header *);
        eth_type = ntohs(eh->ether_type);
        if (eth_type == ETHERTYPE_VLAN) {
            ip_offset = sizeof(struct ether_vlan_header);
            if (m->m_hdr.mh_len < ip_offset) {
                if ((m = m_pullup(m, ip_offset)) == nullptr)
                    return nullptr;
            }
            evh = mtod(m, struct ether_vlan_header *);
            eth_type = ntohs(evh->evl_proto);
        }

        switch (eth_type) {
        case ETHERTYPE_IP:
            if (m->m_hdr.mh_len < ip_offset + (int)sizeof(struct ip)) {
                m = m_pullup(m, ip_offset + sizeof(struct ip));
                if (m == nullptr)
                    return nullptr;
            }

            ip = (struct ip *)(mtod(m, uint8_t *) + ip_offset);
            ip_proto = ip->ip_p;
            csum_start = ip_offset + (ip->ip_hl << 2);
            gso_type = virtio_net::virtio_net_hdr::VIRTIO_NET_HDR_GSO_TCPV4;
            break;

        default:
            return m;
        }

        if (m->M_dat.MH.MH_pkthdr.csum_flags & VIRTIO_NET_CSUM_OFFLOAD) {
            hdr->flags |= virtio_net_hdr::VIRTIO_NET_HDR_F_NEEDS_CSUM;
            hdr->csum_start = csum_start;
            hdr->csum_offset = m->M_dat.MH.MH_pkthdr.csum_data;
        }

        if (m->M_dat.MH.MH_pkthdr.csum_flags & CSUM_TSO) {
            if (ip_proto != IPPROTO_TCP)
                return m;

            if (m->m_hdr.mh_len < csum_start + (int)sizeof(struct tcphdr)) {
                m = m_pullup(m, csum_start + sizeof(struct tcphdr));
                if (m == nullptr)
                    return nullptr;
            }

            tcp = (struct tcphdr *)(mtod(m, uint8_t *) + csum_start);
            hdr->gso_type = gso_type;
            hdr->hdr_len = csum_start + (tcp->th_off << 2);
            hdr->gso_size = m->M_dat.MH.MH_pkthdr.tso_segsz;

            if (tcp->th_flags & TH_CWR) {
                if (!_tso_ecn) {
                    virtio_w("TSO with ECN not supported by host\n");
                    m_freem(m);
                    return nullptr;
                }

                hdr->flags |= virtio_net_hdr::VIRTIO_NET_HDR_GSO_ECN;
            }
        }

        return m;
    }

    void virtio_net::tx_gc()
    {
        virtio_net_req * req;
        u32 len;

        while((req = static_cast<virtio_net_req*>(_tx_queue->get_buf_elem(&len))) != nullptr) {
            delete req;
            _tx_queue->get_buf_finalize();
        }
        _tx_queue->get_buf_gc();
    }

    u32 virtio_net::get_driver_features(void)
    {
        u32 base = virtio_driver::get_driver_features();
        return (base | (1 << VIRTIO_NET_F_MAC)        \
                     | (1 << VIRTIO_NET_F_MRG_RXBUF)  \
                     | (1 << VIRTIO_NET_F_STATUS)     \
                     | (1 << VIRTIO_NET_F_CSUM)       \
                     | (1 << VIRTIO_NET_F_GUEST_CSUM) \
                     | (1 << VIRTIO_NET_F_GUEST_TSO4) \
                     | (1 << VIRTIO_NET_F_HOST_ECN)   \
                     | (1 << VIRTIO_NET_F_HOST_TSO4)  \
                     | (1 << VIRTIO_NET_F_GUEST_ECN)  \
                     | (1 << VIRTIO_RING_F_INDIRECT_DESC));
    }

    hw_driver* virtio_net::probe(hw_device* dev)
    {
        return virtio::probe<virtio_net, VIRTIO_NET_DEVICE_ID>(dev);
    }

}

