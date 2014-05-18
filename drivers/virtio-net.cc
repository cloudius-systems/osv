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
#include <osv/interrupt.hh>

#include <osv/mempool.hh>
#include <osv/mmu.hh>

#include <string>
#include <string.h>
#include <map>
#include <errno.h>
#include <osv/debug.h>

#include <osv/sched.hh>
#include <osv/trace.hh>
#include <osv/net_trace.hh>

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

int net::_instance = 0;

#define net_tag "virtio-net"
#define net_d(...)   tprintf_d(net_tag, __VA_ARGS__)
#define net_i(...)   tprintf_i(net_tag, __VA_ARGS__)
#define net_w(...)   tprintf_w(net_tag, __VA_ARGS__)
#define net_e(...)   tprintf_e(net_tag, __VA_ARGS__)

static int if_ioctl(struct ifnet* ifp, u_long command, caddr_t data)
{
    net_d("if_ioctl %x", command);

    int error = 0;
    switch(command) {
    case SIOCSIFMTU:
        net_d("SIOCSIFMTU");
        break;
    case SIOCSIFFLAGS:
        net_d("SIOCSIFFLAGS");
        /* Change status ifup, ifdown */
        if (ifp->if_flags & IFF_UP) {
            ifp->if_drv_flags |= IFF_DRV_RUNNING;
            net_d("if_up");
        } else {
            ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
            net_d("if_down");
        }
        break;
    case SIOCADDMULTI:
    case SIOCDELMULTI:
        net_d("SIOCDELMULTI");
        break;
    default:
        net_d("redirecting to ether_ioctl()...");
        error = ether_ioctl(ifp, command, data);
        break;
    }

    return error;
}

/**
 * Invalidate the local Tx queues.
 * @param ifp upper layer instance handle
 */
static void if_qflush(struct ifnet* ifp)
{
    //
    // TODO: Add per-CPU Tx queues flushing here. Most easily checked with
    // change MTU use case.
    //
    ::if_qflush(ifp);
}

/**
 * Transmits a single mbuf instance.
 * @param ifp upper layer instance handle
 * @param m_head mbuf to transmit
 *
 * @return 0 in case of success and an appropriate error code
 *         otherwise
 */
static int if_transmit(struct ifnet* ifp, struct mbuf* m_head)
{
    net* vnet = (net*)ifp->if_softc;

    net_d("%s_start", __FUNCTION__);

    return vnet->xmit(m_head);
}

inline int net::xmit(struct mbuf* buff)
{
    //
    // We currently have only a single TX queue. Select a proper TXq here when
    // we implement a multi-queue.
    //
    return _txq.xmit(buff);
}

inline int net::txq::xmit(mbuf* buff)
{
    return _xmitter.xmit(buff);
}

inline bool net::txq::kick_hw()
{
    return vqueue->kick();
}

inline void net::txq::kick_pending(u16 thresh)
{
    if (_pkts_to_kick >= thresh) {
        _pkts_to_kick = 0;
        kick_hw();
    }
}

inline void net::txq::wake_worker()
{
    worker.wake();
}


static void if_init(void* xsc)
{
    net_d("Virtio-net init");
}

/**
 * Return all the statistics we have gathered.
 * @param ifp
 * @param out_data
 */
static void if_getinfo(struct ifnet* ifp, struct if_data* out_data)
{
    net* vnet = (net*)ifp->if_softc;

    // First - take the ifnet data
    memcpy(out_data, &ifp->if_data, sizeof(*out_data));

    // then fill the internal statistics we've gathered
    vnet->fill_stats(out_data);
}

void net::fill_stats(struct if_data* out_data) const
{
    // We currently support only a single Tx/Rx queue so no iteration so far
    fill_qstats(_rxq, out_data);
    fill_qstats(_txq, out_data);
}

void net::fill_qstats(const struct rxq& rxq,
                             struct if_data* out_data) const
{
    out_data->ifi_ipackets += rxq.stats.rx_packets;
    out_data->ifi_ibytes   += rxq.stats.rx_bytes;
    out_data->ifi_iqdrops  += rxq.stats.rx_drops;
    out_data->ifi_ierrors  += rxq.stats.rx_csum_err;
}

void net::fill_qstats(const struct txq& txq,
                      struct if_data* out_data) const
{
    assert(!out_data->ifi_oerrors && !out_data->ifi_obytes && !out_data->ifi_opackets);
    out_data->ifi_opackets += txq.stats.tx_packets;
    out_data->ifi_obytes   += txq.stats.tx_bytes;
    out_data->ifi_oerrors  += txq.stats.tx_err + txq.stats.tx_drops;
}

bool net::ack_irq()
{
    auto isr = virtio_conf_readb(VIRTIO_PCI_ISR);

    if (isr) {
        _rxq.vqueue->disable_interrupts();
        return true;
    } else {
        return false;
    }

}

net::net(pci::device& dev)
    : virtio_driver(dev),
      _rxq(get_virt_queue(0), [this] { this->receiver(); }),
      _txq(this, get_virt_queue(1))
{
    sched::thread* poll_task = &_rxq.poll_task;
    sched::thread* tx_worker_task = &_txq.worker;

    _driver_name = "virtio-net";
    virtio_i("VIRTIO NET INSTANCE");
    _id = _instance++;

    setup_features();
    read_config();

    _hdr_size = _mergeable_bufs ? sizeof(net_hdr_mrg_rxbuf) : sizeof(net_hdr);

    //initialize the BSD interface _if
    _ifn = if_alloc(IFT_ETHER);
    if (_ifn == NULL) {
       //FIXME: need to handle this case - expand the above function not to allocate memory and
       // do it within the constructor.
       net_w("if_alloc failed!");
       return;
    }

    if_initname(_ifn, "eth", _id);
    _ifn->if_mtu = ETHERMTU;
    _ifn->if_softc = static_cast<void*>(this);
    _ifn->if_flags = IFF_BROADCAST /*| IFF_MULTICAST*/;
    _ifn->if_ioctl = if_ioctl;
    _ifn->if_transmit = if_transmit;
    _ifn->if_qflush = if_qflush;
    _ifn->if_init = if_init;
    _ifn->if_getinfo = if_getinfo;
    IFQ_SET_MAXLEN(&_ifn->if_snd, _txq.vqueue->size());

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

    _ifn->if_capenable = _ifn->if_capabilities | IFCAP_HWSTATS;

    //
    // Enable indirect descriptors utilization.
    //
    // TODO:
    // Optimize the indirect descriptors infrastructure:
    //  - Preallocate a ring of indirect descriptors per vqueue.
    //  - Consume/recycle from this pool while u can.
    //  - If there is no more free descriptors in the pool above - allocate like
    //    we do today.
    //
    _txq.vqueue->set_use_indirect(true);

    //Start the polling thread before attaching it to the Rx interrupt
    poll_task->start();

    // TODO: What if_init() is for?
    tx_worker_task->start();

    ether_ifattach(_ifn, _config.mac);

    if (dev.is_msix()) {
        _msi.easy_register({
            { 0, [&] { _rxq.vqueue->disable_interrupts(); }, poll_task },
            { 1, [&] { _txq.vqueue->disable_interrupts(); }, nullptr }
        });
    } else {
        _gsi.set_ack_and_handler(dev.get_interrupt_line(),
            [=] { return this->ack_irq(); }, [=] { poll_task->wake(); });
    }

    fill_rx_ring();

    add_dev_status(VIRTIO_CONFIG_S_DRIVER_OK);
}

net::~net()
{
    //TODO: In theory maintain the list of free instances and gc it
    // including the thread objects and their stack
    // Will need to clear the pending requests in the ring too

    // TODO: add a proper cleanup for a rx.poll_task() here.
    //
    // Since this will involve the rework of the virtio layer - make it for
    // all virtio drivers in a separate patchset.

    ether_ifdetach(_ifn);
    if_free(_ifn);
}

void net::read_config()
{
    //read all of the net config  in one shot
    virtio_conf_read(virtio_pci_config_offset(), &_config, sizeof(_config));

    if (get_guest_feature_bit(VIRTIO_NET_F_MAC))
        net_i("The mac addr of the device is %x:%x:%x:%x:%x:%x",
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
    _guest_ufo = get_guest_feature_bit(VIRTIO_NET_F_GUEST_UFO);

    net_i("Features: %s=%d,%s=%d", "Status", _status, "TSO_ECN", _tso_ecn);
    net_i("Features: %s=%d,%s=%d", "Host TSO ECN", _host_tso_ecn, "CSUM", _csum);
    net_i("Features: %s=%d,%s=%d", "Guest_csum", _guest_csum, "guest tso4", _guest_tso4);
    net_i("Features: %s=%d", "host tso4", _host_tso4);
}

/**
 * Original comment from FreeBSD
 * Alternative method of doing receive checksum offloading. Rather
 * than parsing the received frame down to the IP header, use the
 * csum_offset to determine which CSUM_* flags are appropriate. We
 * can get by with doing this only because the checksum offsets are
 * unique for the things we care about.
 *
 * @return true if csum is bad and false if csum is ok (!!!)
 */
bool net::bad_rx_csum(struct mbuf* m, struct net_hdr* hdr)
{
    struct ether_header* eh;
    struct ether_vlan_header* evh;
    struct udphdr* udp;
    int csum_len;
    u16 eth_type;

    csum_len = hdr->csum_start + hdr->csum_offset;

    if (csum_len < (int)sizeof(struct ether_header) + (int)sizeof(struct ip))
        return true;
    if (m->m_hdr.mh_len < csum_len)
        return true;

    eh = mtod(m, struct ether_header*);
    eth_type = ntohs(eh->ether_type);
    if (eth_type == ETHERTYPE_VLAN) {
        evh = mtod(m, struct ether_vlan_header*);
        eth_type = ntohs(evh->evl_proto);
    }

    // How come - no support for IPv6?!
    if (eth_type != ETHERTYPE_IP) {
        return true;
    }

    /* Use the offset to determine the appropriate CSUM_* flags. */
    switch (hdr->csum_offset) {
    case offsetof(struct udphdr, uh_sum):
        if (m->m_hdr.mh_len < hdr->csum_start + (int)sizeof(struct udphdr))
            return true;
        udp = (struct udphdr*)(mtod(m, uint8_t*) + hdr->csum_start);
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

void net::receiver()
{
    vring* vq = _rxq.vqueue;
    std::vector<iovec> packet;

    while (1) {

        // Wait for rx queue (used elements)
        virtio_driver::wait_for_queue(vq, &vring::used_ring_not_empty);
        trace_virtio_net_rx_wake();

        u32 len;
        int nbufs;
        u64 rx_drops = 0, rx_packets = 0, csum_ok = 0;
        u64 csum_err = 0, rx_bytes = 0;

        // use local header that we copy out of the mbuf since we're
        // truncating it.
        net_hdr_mrg_rxbuf* mhdr;

        while (void* page = vq->get_buf_elem(&len)) {

            // TODO: should get out of the loop
            vq->get_buf_finalize();

            // Bad packet/buffer - discard and continue to the next one
            if (len < _hdr_size + ETHER_HDR_LEN) {
                rx_drops++;
                memory::free_page(page);

                continue;
            }

            mhdr = static_cast<net_hdr_mrg_rxbuf*>(page);

            if (!_mergeable_bufs) {
                nbufs = 1;
            } else {
                nbufs = mhdr->num_buffers;
            }

            packet.push_back({page + _hdr_size, len - _hdr_size});

            // Read the fragments
            while (--nbufs > 0) {
                page = vq->get_buf_elem(&len);
                if (!page) {
                    rx_drops++;
                    for (auto&& v : packet) {
                        free_buffer(v);
                    }
                    break;
                }
                packet.push_back({page, len});
                vq->get_buf_finalize();
            }

            auto m_head = packet_to_mbuf(packet);
            packet.clear();

            if ((_ifn->if_capenable & IFCAP_RXCSUM) &&
                (mhdr->hdr.flags &
                 net_hdr::VIRTIO_NET_HDR_F_NEEDS_CSUM)) {
                if (bad_rx_csum(m_head, &mhdr->hdr))
                    csum_err++;
                else
                    csum_ok++;

            }

            rx_packets++;
            rx_bytes += m_head->M_dat.MH.MH_pkthdr.len;

            bool fast_path = _ifn->if_classifier.post_packet(m_head);
            if (!fast_path) {
                (*_ifn->if_input)(_ifn, m_head);
            }

            trace_virtio_net_rx_packet(_ifn->if_index, rx_bytes);

            // The interface may have been stopped while we were
            // passing the packet up the network stack.
            if ((_ifn->if_drv_flags & IFF_DRV_RUNNING) == 0)
                break;
        }

        if (vq->refill_ring_cond())
            fill_rx_ring();

        // Update the stats
        _rxq.stats.rx_drops      += rx_drops;
        _rxq.stats.rx_packets    += rx_packets;
        _rxq.stats.rx_csum       += csum_ok;
        _rxq.stats.rx_csum_err   += csum_err;
        _rxq.stats.rx_bytes      += rx_bytes;
    }
}

mbuf* net::packet_to_mbuf(const std::vector<iovec>& packet)
{
    auto m = m_gethdr(M_DONTWAIT, MT_DATA);
    auto refcnt = new unsigned;
    m->M_dat.MH.MH_dat.MH_ext.ref_cnt = refcnt;
    m_extadd(m, static_cast<char*>(packet[0].iov_base), packet[0].iov_len,
            &net::free_buffer_and_refcnt, packet[0].iov_base, refcnt, M_PKTHDR, EXT_EXTREF);
    m->M_dat.MH.MH_pkthdr.len = packet[0].iov_len;
    m->M_dat.MH.MH_pkthdr.rcvif = _ifn;
    m->M_dat.MH.MH_pkthdr.csum_flags = 0;
    m->m_hdr.mh_len = packet[0].iov_len;
    m->m_hdr.mh_next = nullptr;

    auto m_head = m;
    auto m_tail = m;
    for (size_t idx = 1; idx != packet.size(); ++idx) {
        auto&& iov = packet[idx];
        auto m = m_get(M_DONTWAIT, MT_DATA);
        refcnt = new unsigned;
        m->M_dat.MH.MH_dat.MH_ext.ref_cnt = refcnt;
        m_extadd(m, static_cast<char*>(iov.iov_base), iov.iov_len,
                &net::free_buffer_and_refcnt, iov.iov_base, refcnt, 0, EXT_EXTREF);
        m->m_hdr.mh_len = iov.iov_len;
        m->m_hdr.mh_next = nullptr;
        m_tail->m_hdr.mh_next = m;
        m_tail = m;
        m_head->M_dat.MH.MH_pkthdr.len += iov.iov_len;
    }
    return m_head;
}

// hook for EXT_EXTREF mbuf cleanup
void net::free_buffer_and_refcnt(void* buffer, void* refcnt)
{
    do_free_buffer(buffer);
    delete static_cast<unsigned*>(refcnt);
}

void net::do_free_buffer(void* buffer)
{
    buffer = align_down(buffer, page_size);
    memory::free_page(buffer);
}

void net::fill_rx_ring()
{
    trace_virtio_net_fill_rx_ring(_ifn->if_index);
    int added = 0;
    vring* vq = _rxq.vqueue;

    while (vq->avail_ring_not_empty()) {
        auto page = memory::alloc_page();

        vq->init_sg();
        vq->add_in_sg(page, memory::page_size);
        if (!vq->add_buf(page)) {
            memory::free_page(page);
            break;
        }
        added++;
    }

    trace_virtio_net_fill_rx_ring_added(_ifn->if_index, added);

    if (added)
        vq->kick();
}

inline int net::txq::try_xmit_one_locked(void* _req)
{
    net_req* req = static_cast<net_req*>(_req);
    int rc = try_xmit_one_locked(req);

    if (rc) {
        return rc;
    }

    update_stats(req);
    return 0;
}

inline int net::txq::xmit_prep(mbuf* m_head, void*& cooky)
{
    net_req* req = new net_req(m_head);
    mbuf* m;

    if (m_head->M_dat.MH.MH_pkthdr.csum_flags != 0) {
        m = offload(m_head, &req->mhdr.hdr);
        if ((m_head = m) == nullptr) {
            stats.tx_err++;

            delete req;

            /* The buffer is not well-formed */
            return EINVAL;
        }
    }

    cooky = req;
    return 0;
}

int net::txq::try_xmit_one_locked(net_req* req)
{
    mbuf *m_head = req->mb, *m;
    u16 vec_sz = 1;
    u64 tx_bytes = 0;

    if (_parent->_mergeable_bufs) {
        req->mhdr.num_buffers = 0;
    }

    vqueue->init_sg();
    vqueue->add_out_sg(static_cast<void*>(&req->mhdr),
                       sizeof(net_hdr_mrg_rxbuf));

    for (m = m_head; m != NULL; m = m->m_hdr.mh_next) {
        int frag_len = m->m_hdr.mh_len;

        if (frag_len != 0) {
            net_d("Frag len=%d:", frag_len);
            vec_sz++;
            tx_bytes += frag_len;
            vqueue->add_out_sg(m->m_hdr.mh_data, m->m_hdr.mh_len);
        }
    }

    req->tx_bytes = tx_bytes;

    if (!vqueue->avail_ring_has_room(vec_sz)) {
        if (vqueue->used_ring_not_empty()) {
            trace_virtio_net_tx_no_space_calling_gc(_parent->_ifn->if_index);
            gc();
            if (!vqueue->avail_ring_has_room(vec_sz)) {
                return ENOBUFS;
            }
        } else {
            return ENOBUFS;
        }
    }

    if (!vqueue->add_buf(req)) {
        assert(0);
    }

    return 0;
}

inline void net::txq::update_stats(net_req* req)
{
    stats.tx_bytes += req->tx_bytes;
    stats.tx_packets++;

    if (req->mhdr.hdr.flags & net_hdr::VIRTIO_NET_HDR_F_NEEDS_CSUM)
        stats.tx_csum++;

    if (req->mhdr.hdr.gso_type)
        stats.tx_tso++;
}


void net::txq::xmit_one_locked(void* _req)
{
    net_req* req = static_cast<net_req*>(_req);

    if (try_xmit_one_locked(req)) {
        do {
            // We are going to poll - flush the pending packets
            kick_pending();
            if (!vqueue->used_ring_not_empty()) {
                do {
                    sched::thread::yield();
                } while (!vqueue->used_ring_not_empty());
            }
            gc();
        } while (!vqueue->add_buf(req));
    }

    trace_virtio_net_tx_packet(_parent->_ifn->if_index, vqueue->_sg_vec.size());

    // Update the statistics
    update_stats(req);

    //
    // It was a good packet - increase the counter of a "pending for a kick"
    // packets.
    //
    _pkts_to_kick++;
}

mbuf* net::txq::offload(mbuf* m, net_hdr* hdr)
{
    struct ether_header* eh;
    struct ether_vlan_header* evh;
    struct ip* ip;
    struct tcphdr* tcp;
    int ip_offset;
    u16 eth_type, csum_start;
    u8 ip_proto, gso_type = 0;

    ip_offset = sizeof(struct ether_header);
    if (m->m_hdr.mh_len < ip_offset) {
        if ((m = m_pullup(m, ip_offset)) == nullptr)
            return nullptr;
    }

    eh = mtod(m, struct ether_header*);
    eth_type = ntohs(eh->ether_type);
    if (eth_type == ETHERTYPE_VLAN) {
        ip_offset = sizeof(struct ether_vlan_header);
        if (m->m_hdr.mh_len < ip_offset) {
            if ((m = m_pullup(m, ip_offset)) == nullptr)
                return nullptr;
        }
        evh = mtod(m, struct ether_vlan_header*);
        eth_type = ntohs(evh->evl_proto);
    }

    switch (eth_type) {
    case ETHERTYPE_IP:
        if (m->m_hdr.mh_len < ip_offset + (int)sizeof(struct ip)) {
            m = m_pullup(m, ip_offset + sizeof(struct ip));
            if (m == nullptr)
                return nullptr;
        }

        ip = (struct ip*)(mtod(m, uint8_t*) + ip_offset);
        ip_proto = ip->ip_p;
        csum_start = ip_offset + (ip->ip_hl << 2);
        gso_type = net::net_hdr::VIRTIO_NET_HDR_GSO_TCPV4;
        break;

    default:
        return m;
    }

    if (m->M_dat.MH.MH_pkthdr.csum_flags & VIRTIO_NET_CSUM_OFFLOAD) {
        hdr->flags |= net_hdr::VIRTIO_NET_HDR_F_NEEDS_CSUM;
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

        tcp = (struct tcphdr*)(mtod(m, uint8_t*) + csum_start);
        hdr->gso_type = gso_type;
        hdr->hdr_len = csum_start + (tcp->th_off << 2);
        hdr->gso_size = m->M_dat.MH.MH_pkthdr.tso_segsz;

        if (tcp->th_flags & TH_CWR) {
            if (!_parent->_tso_ecn) {
                virtio_w("TSO with ECN not supported by host\n");
                m_freem(m);
                return nullptr;
            }

            hdr->flags |= net_hdr::VIRTIO_NET_HDR_GSO_ECN;
        }
    }

    return m;
}

void net::txq::gc()
{
    net_req* req;
    u32 len;
    u16 req_cnt = 0;

    //
    // "finalize" at least every quarter of a ring to let the host work in
    // parallel with us.
    //
    const u16 fin_thr = static_cast<u16>(vqueue->size()) / 4;

    req = static_cast<net_req*>(vqueue->get_buf_elem(&len));

    while(req != nullptr) {
        m_freem(req->mb);
        delete req;

        req_cnt++;

        if (req_cnt >= fin_thr) {
            vqueue->get_buf_finalize(true);
            req_cnt = 0;
        } else {
            vqueue->get_buf_finalize(false);
        }

        req = static_cast<net_req*>(vqueue->get_buf_elem(&len));
    }

    if (req_cnt) {
        vqueue->update_used_event();
    }

    vqueue->get_buf_gc();
}

u32 net::get_driver_features()
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
                 | (1 << VIRTIO_NET_F_GUEST_ECN)
                 | (1 << VIRTIO_NET_F_GUEST_UFO)
            );
}

hw_driver* net::probe(hw_device* dev)
{
    return virtio::probe<net, VIRTIO_NET_DEVICE_ID>(dev);
}

} // namespace virtio

