/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */


#include <osv/drivers_config.h>
#include <sys/cdefs.h>

#include "drivers/virtio.hh"
#include "drivers/virtio-net.hh"
#include <osv/interrupt.hh>

#include <osv/mempool.hh>
#include <osv/mmu.hh>

#include <string>
#include <string.h>
#include <map>
#include <algorithm>
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

TRACEPOINT(trace_virtio_net_tx_packet_size, "vring %p vec_sz %d", void*, int);
TRACEPOINT(trace_virtio_net_tx_xmit_one_failed_to_post, "vring %p vec_sz %d",
           void*, int);

using namespace memory;

// TODO list
// irq thread affinity and tx affinity
// tx zero copy
// vlans?

extern bool opt_maxnic;
extern int maxnic;

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
        net_d("SIOCADDMULTI");
        break;
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
    // Select a Tx queue by CPU id so that transmits from different CPUs use
    // independent rings and do not serialize on one queue.  With a single
    // queue pair (_nqp == 1) this always selects queue 0, i.e. the original
    // behaviour.
    //
    unsigned qidx = sched::cpu::current()->id % _nqp;
    return _txqs[qidx]->xmit(buff);
}

inline int net::txq::xmit(mbuf* buff)
{
    return _xmitter.xmit(buff);
}

inline bool net::txq::kick_hw()
{
    bool kicked = vqueue->kick();
    stats.tx_kicks += !!kicked;

    return kicked;
}

inline void net::txq::kick_pending(u16 thresh)
{
    if (_pkts_to_kick >= thresh) {
        _pkts_to_kick = 0;
        stats.tx_worker_kicks += !!kick_hw();
    }
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
    // Aggregate the per-queue statistics across all Rx/Tx queue pairs.
    for (auto& rxq : _rxqs) {
        fill_qstats(*rxq, out_data);
    }
    for (auto& txq : _txqs) {
        fill_qstats(*txq, out_data);
    }
}

void net::fill_qstats(const struct rxq& rxq, struct if_data* out_data) const
{
    out_data->ifi_ipackets   += rxq.stats.rx_packets;
    out_data->ifi_ibytes     += rxq.stats.rx_bytes;
    out_data->ifi_iqdrops    += rxq.stats.rx_drops;
    out_data->ifi_ierrors    += rxq.stats.rx_csum_err;
    out_data->ifi_ibh_wakeups = rxq.stats.rx_bh_wakeups;
    out_data->ifi_iwakeup_stats = rxq.stats.rx_wakeup_stats;
}

void net::fill_qstats(const struct txq& txq, struct if_data* out_data) const
{
    // Called once per Tx queue and accumulated, so every field must add rather
    // than overwrite (with multiqueue there is more than one Tx queue).
    out_data->ifi_opackets       += txq.stats.tx_packets;
    out_data->ifi_obytes         += txq.stats.tx_bytes;
    out_data->ifi_oerrors        += txq.stats.tx_err + txq.stats.tx_drops;
    out_data->ifi_oworker_kicks  += txq.stats.tx_worker_kicks;
    out_data->ifi_oworker_wakeups += txq.stats.tx_worker_wakeups;
    out_data->ifi_oworker_packets += txq.stats.tx_worker_packets;
    out_data->ifi_okicks         += txq.stats.tx_kicks;
    out_data->ifi_oqueue_is_full += txq.stats.tx_hw_queue_is_full;
    out_data->ifi_owakeup_stats   = txq.stats.tx_wakeup_stats;
}

bool net::ack_irq()
{
    auto isr = _dev.read_and_ack_isr();

    if (isr) {
        // Only used on the shared single-interrupt (MMIO/legacy) path, where
        // there is a single queue pair; disable the Rx queue's interrupts.
        _rxqs[0]->vqueue->disable_interrupts();
        return true;
    } else {
        return false;
    }
}

void net::init()
{
    // Steps 4, 5 & 6 - negotiate and confirm features
    setup_features();
    read_config();

    // Step 7 - generic init of virtqueues
    probe_virt_queues();
}

net::net(virtio_device& dev)
    : virtio_driver(dev),
    _pre_init(this)
{
    _driver_name = "virtio-net";
    virtio_i("VIRTIO NET INSTANCE");
    _id = _instance++;

    // Please look at the section 5.1.6.1 of virtio specification for explanation
    if (_dev.is_modern()) {
        _hdr_size = sizeof(net_hdr_mrg_rxbuf);
    }
    else {
        _hdr_size = _mergeable_bufs ? sizeof(net_hdr_mrg_rxbuf) : sizeof(net_hdr);
    }

    //
    // Decide the number of Rx/Tx queue pairs to use.  With VIRTIO_NET_F_MQ the
    // device advertises max_virtqueue_pairs and lays out the virtqueues as
    // rx[i]=2*i, tx[i]=2*i+1, ctrl=2*nqp.  probe_virt_queues() (run from init()
    // via _pre_init) has already created every virtqueue the device exposes, so
    // _num_queues tells us how many actually exist.  We use one queue pair per
    // vCPU, bounded by what the device advertises, what was probed, and the
    // shared MSI-X vector budget (2 vectors per pair).  When F_MQ is absent this
    // collapses to a single pair - identical to the original driver.
    unsigned nqp = 1;
    // Activating more than one queue pair requires the control virtqueue to
    // send CTRL_MQ_VQ_PAIRS_SET, so multiqueue is gated on both features.
    if (get_guest_feature_bit(VIRTIO_NET_F_MQ) &&
        get_guest_feature_bit(VIRTIO_NET_F_CTRL_VQ) &&
        _config.max_virtqueue_pairs > 1) {
        nqp = std::min<unsigned>(_config.max_virtqueue_pairs, sched::cpus.size());
        // Number of pairs the probed virtqueues can support: N pairs need
        // 2*N data queues plus one control queue.
        unsigned probed_pairs = _num_queues > 1 ? (_num_queues - 1) / 2 : 1;
        nqp = std::min(nqp, probed_pairs);
        if (nqp < 1) {
            nqp = 1;
        }
        // Cap against the shared MSI-X vector budget (2 vectors per pair).
        unsigned vec_pairs = reserve_msix_vectors(2 * nqp) / 2;
        if (vec_pairs < 1) {
            vec_pairs = 1;
        }
        nqp = std::min(nqp, vec_pairs);
    }
    _nqp = nqp;
    // Per the virtio spec the control virtqueue always sits at index
    // 2*max_virtqueue_pairs (based on the device's advertised maximum), which
    // is not the same as 2*_nqp when we activate fewer pairs than advertised.
    if (get_guest_feature_bit(VIRTIO_NET_F_CTRL_VQ)) {
        unsigned max_pairs = _config.max_virtqueue_pairs > 1 ?
            _config.max_virtqueue_pairs : 1;
        int ctrl_idx = 2 * max_pairs;
        if ((unsigned)ctrl_idx < _num_queues) {
            _ctrl_vq_idx = ctrl_idx;
        }
    }

    // Build the Rx/Tx queue pairs.  Each Rx queue gets its own poll thread; each
    // Tx queue its own xmitter.
    for (unsigned i = 0; i < _nqp; i++) {
        auto* rx_vq = get_virt_queue(2 * i);
        auto* tx_vq = get_virt_queue(2 * i + 1);
        assert(rx_vq && tx_vq);
        _rxqs.push_back(std::unique_ptr<rxq>(
            new rxq(rx_vq, [this, i] { this->receiver(this->_rxqs[i].get()); })));
        _txqs.push_back(std::unique_ptr<txq, aligned_new_deleter<txq>>(
            aligned_new<txq>(this, tx_vq)));
        _rxqs[i]->poll_task->set_priority(sched::thread::priority_infinity);
    }

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
    _ifn->if_flags = IFF_BROADCAST | IFF_MULTICAST;
    _ifn->if_ioctl = if_ioctl;
    _ifn->if_transmit = if_transmit;
    _ifn->if_qflush = if_qflush;
    _ifn->if_init = if_init;
    _ifn->if_getinfo = if_getinfo;
    IFQ_SET_MAXLEN(&_ifn->if_snd, _txqs[0]->vqueue->size());

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

    //Start the polling threads before attaching them to the Rx interrupts
    for (unsigned i = 0; i < _nqp; i++) {
        _rxqs[i]->poll_task->start();
        _txqs[i]->start();
    }

    ether_ifattach(_ifn, _config.mac);

    // Pin each Rx poll thread to a distinct CPU (round-robin) so that with
    // multiqueue the per-queue receivers run in parallel across CPUs instead of
    // contending for one.  With a single queue pair this pins to CPU 0, which
    // matches the priority_infinity single-thread behaviour of the original
    // driver closely enough and keeps its Rx work on one CPU.
    for (unsigned i = 0; i < _nqp; i++) {
        sched::thread::pin(_rxqs[i]->poll_task.get(), sched::cpus[i % sched::cpus.size()]);
    }

    interrupt_factory int_factory;
#if CONF_drivers_pci
    // One MSI-X vector per data virtqueue.  setup_queue() maps virtqueue index
    // i -> MSI-X entry i (1:1), so the entry numbers are the virtqueue indices
    // rx[i]=2*i and tx[i]=2*i+1.  Each Rx vector wakes that queue's poll thread;
    // Tx vectors just disable the queue's interrupts (Tx completion is reaped by
    // the xmitter/gc path, as in the original single-queue driver).
    int_factory.register_msi_bindings = [this](interrupt_manager &msi) {
        std::vector<msix_binding> bindings;
        bindings.reserve(2 * _nqp);
        for (unsigned i = 0; i < _nqp; i++) {
            auto* rx = _rxqs[i].get();
            auto* tx = _txqs[i].get();
            sched::thread* poll = rx->poll_task.get();
            bindings.push_back({ (unsigned)(2 * i),
                                 [rx] { rx->vqueue->disable_interrupts(); }, poll });
            bindings.push_back({ (unsigned)(2 * i + 1),
                                 [tx] { tx->vqueue->disable_interrupts(); }, nullptr });
        }
        if (!msi.easy_register(bindings)) {
            // Vector allocation is bounded by reserve_msix_vectors() above, so
            // this should not happen; if it does, fail loudly rather than run
            // with queues whose completions are never serviced.
            net_e("virtio-net: failed to register %d MSI-X vectors", 2 * _nqp);
            abort();
        }
    };

    int_factory.create_pci_interrupt = [this](pci::device &pci_dev) {
        return new pci_interrupt(
            pci_dev,
            [=] { return this->ack_irq(); },
            [=] { this->_rxqs[0]->poll_task->wake_with_irq_disabled(); });
    };
#endif

#if CONF_drivers_mmio
    // The MMIO transport has a single shared interrupt line, so multiqueue Rx
    // steering is not available there; _nqp is 1 on that path.
    sched::thread* poll_task = _rxqs[0]->poll_task.get();
#ifdef __aarch64__
    int_factory.create_spi_edge_interrupt = [this,poll_task]() {
        return new spi_interrupt(
            gic::irq_type::IRQ_TYPE_EDGE,
            _dev.get_irq(),
            [=] { return this->ack_irq(); },
            [=] { poll_task->wake_with_irq_disabled(); });
    };
#else
    int_factory.create_gsi_edge_interrupt = [this,poll_task]() {
        return new gsi_edge_interrupt(
            _dev.get_irq(),
            [=] { if (this->ack_irq()) poll_task->wake_with_irq_disabled(); });
    };
#endif
#endif

    _dev.register_interrupt(int_factory);

    // Tell the device how many queue pairs we will actually use.  Must happen
    // after the queues and their interrupts are set up and before we start
    // feeding the Rx rings.
    if (_nqp > 1 && _ctrl_vq_idx >= 0) {
        if (!set_vq_pairs(_nqp)) {
            net_w("virtio-net: CTRL_MQ_VQ_PAIRS_SET(%d) not acked by device", _nqp);
        }
    }

    for (unsigned i = 0; i < _nqp; i++) {
        fill_rx_ring(_rxqs[i].get());
    }

    // Step 8
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
    virtio_conf_read(0, &(_config.mac[0]), sizeof(_config.mac));

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

    // With VIRTIO_NET_F_MQ the device advertises how many Rx/Tx queue pairs it
    // supports; read it so the constructor can decide how many to use.  Default
    // to a single pair otherwise.
    _config.max_virtqueue_pairs = 1;
    if (get_guest_feature_bit(VIRTIO_NET_F_MQ)) {
        virtio_conf_read(offsetof(net_config, max_virtqueue_pairs),
                         &_config.max_virtqueue_pairs,
                         sizeof(_config.max_virtqueue_pairs));
        net_i("Multiqueue: device advertises %d queue pairs",
              _config.max_virtqueue_pairs);
    }

    net_i("Features: %s=%d,%s=%d", "Status", _status, "TSO_ECN", _tso_ecn);
    net_i("Features: %s=%d,%s=%d", "Host TSO ECN", _host_tso_ecn, "CSUM", _csum);
    net_i("Features: %s=%d,%s=%d", "Guest_csum", _guest_csum, "guest tso4", _guest_tso4);
    net_i("Features: %s=%d,%s=%d", "host tso4", _host_tso4, "MRG_RX_BUF", _mergeable_bufs);

    // If VIRTIO_NET_F_MRG_RXBUF is not negotiated and VIRTIO_NET_F_GUEST_TSO4
    // or VIRTIO_NET_F_GUEST_UFO are, the VirtIO spec mandates the guest to use
    // large receive buffers
    // For details please see "5.1.6.3.1 Driver Requirements: Setting Up Receive Buffers" in VirtIO spec
    if (!_mergeable_bufs && (_guest_tso4 || _guest_ufo)) {
        net_i("Set up to use large receive buffers");
        _use_large_buffers = true;
    }
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

void net::receiver(struct rxq* rxq)
{
    vring* vq = rxq->vqueue;
    std::vector<iovec> packet;
    u64 rx_drops = 0, rx_packets = 0, csum_ok = 0;
    u64 csum_err = 0, rx_bytes = 0;
    static const u16 refill_thresh = 16;

    while (1) {

        // Wait for rx queue (used elements)
        virtio_driver::wait_for_queue(vq, &vring::used_ring_not_empty);
        trace_virtio_net_rx_wake();

        rxq->stats.rx_bh_wakeups++;
        rxq->update_wakeup_stats(rx_packets);

        u32 len;
        int nbufs;
        rx_drops = rx_packets = csum_ok = 0;
        csum_err = rx_bytes = 0;

        // use local header that we copy out of the mbuf since we're
        // truncating it.
        net_hdr_mrg_rxbuf* mhdr;

        while (void* buffer = vq->get_buf_elem(&len)) {

            vq->get_buf_finalize();

            if (vq->effective_avail_ring_count() >= refill_thresh)
                fill_rx_ring(rxq);

            // Bad packet/buffer - discard and continue to the next one
            if (len < _hdr_size + ETHER_HDR_LEN) {
                rx_drops++;
                free_buffer(buffer);
                continue;
            }

            mhdr = static_cast<net_hdr_mrg_rxbuf*>(buffer);

            if (!_mergeable_bufs) {
                nbufs = 1;
            } else {
                nbufs = mhdr->num_buffers;
            }

            packet.push_back({buffer + _hdr_size, len - _hdr_size});

            // Read the fragments - only applies if _mergeable_bufs is ON
            while (--nbufs > 0) {
                buffer = vq->get_buf_elem(&len);
                if (!buffer) {
                    rx_drops++;
                    for (auto&& v : packet) {
                        free_buffer(v);
                    }
                    break;
                }
                packet.push_back({buffer, len});
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

        // Update the stats
        rxq->stats.rx_drops      += rx_drops;
        rxq->stats.rx_packets    += rx_packets;
        rxq->stats.rx_csum       += csum_ok;
        rxq->stats.rx_csum_err   += csum_err;
        rxq->stats.rx_bytes      += rx_bytes;
    }
}

// The mbuf external-storage reference count lives inside the receive buffer
// itself: fill_rx_ring() reserves sizeof(unsigned) bytes at the tail of each
// buffer (see rx_buffer_refcnt()) and never advertises them to the device, so
// they are private scratch that is freed together with the buffer.  This keeps
// the receive fast path free of a per-packet heap allocation for the refcount.
mbuf* net::packet_to_mbuf(const std::vector<iovec>& packet)
{
    auto m = m_gethdr(M_DONTWAIT, MT_DATA);
    auto refcnt = rx_buffer_refcnt(packet[0].iov_base);
    m->M_dat.MH.MH_dat.MH_ext.ref_cnt = refcnt;
    m_extadd(m, static_cast<char*>(packet[0].iov_base), packet[0].iov_len,
            _use_large_buffers ? &net::free_large_rx_buffer : &net::free_rx_buffer,
            packet[0].iov_base, refcnt, M_PKTHDR, EXT_EXTREF);
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
        refcnt = rx_buffer_refcnt(iov.iov_base);
        m->M_dat.MH.MH_dat.MH_ext.ref_cnt = refcnt;
        m_extadd(m, static_cast<char*>(iov.iov_base), iov.iov_len,
                _use_large_buffers ? &net::free_large_rx_buffer : &net::free_rx_buffer,
                iov.iov_base, refcnt, 0, EXT_EXTREF);
        m->m_hdr.mh_len = iov.iov_len;
        m->m_hdr.mh_next = nullptr;
        m_tail->m_hdr.mh_next = m;
        m_tail = m;
        m_head->M_dat.MH.MH_pkthdr.len += iov.iov_len;
    }
    return m_head;
}

// hook for EXT_EXTREF mbuf cleanup.  The refcount lives inside the buffer (see
// rx_buffer_refcnt / fill_rx_ring), so freeing the buffer frees it too; the
// refcnt argument m_extadd stored is ignored here.
void net::free_rx_buffer(void* buffer, void* refcnt)
{
    do_free_buffer(buffer);
}

void net::free_large_rx_buffer(void* buffer, void* refcnt)
{
    do_free_large_buffer(buffer);
}

void net::do_free_buffer(void* buffer)
{
    buffer = align_down(buffer, page_size);
    memory::free_page(buffer);
}

void net::do_free_large_buffer(void* buffer)
{
    buffer = align_down(buffer, page_size);
    memory::free_phys_contiguous_aligned(buffer);
}

// The refcount for a receive buffer lives in the sizeof(unsigned) bytes
// reserved at the buffer's tail by fill_rx_ring().  frame points at packet
// data inside the buffer (base + header for the head fragment, base for a
// continuation fragment); the buffer base is the page it sits in, and the
// total buffer size depends on whether large buffers are in use.
unsigned* net::rx_buffer_refcnt(void* frame)
{
    auto base = static_cast<char*>(align_down(frame, page_size));
    size_t buf_bytes = (_use_large_buffers ? LARGE_BUFFER_SIZE_IN_PAGES : 1)
                       * memory::page_size;
    return reinterpret_cast<unsigned*>(base + buf_bytes - sizeof(unsigned));
}

void net::fill_rx_ring(struct rxq* rxq)
{
    trace_virtio_net_fill_rx_ring(_ifn->if_index);
    int added = 0;
    vring* vq = rxq->vqueue;

    int size_in_pages = _use_large_buffers ? LARGE_BUFFER_SIZE_IN_PAGES : 1;
    // Reserve sizeof(unsigned) at the tail of every buffer for that buffer's
    // mbuf external-storage refcount (see rx_buffer_refcnt).  The reserved
    // bytes are never advertised to the device, so the device never writes
    // over the refcount.
    size_t buf_bytes = size_in_pages * memory::page_size;
    size_t dev_bytes = buf_bytes - sizeof(unsigned);
    while (vq->avail_ring_not_empty()) {
        void *buffer;
        if (_use_large_buffers) {
            buffer = memory::alloc_phys_contiguous_aligned(buf_bytes, memory::page_size);
        } else {
            buffer = memory::alloc_page();
        }

        vq->init_sg();
        vq->add_in_sg(buffer, dev_bytes);
        if (!vq->add_buf(buffer)) {
            free_buffer(buffer);
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
    u16 vec_sz;
    u64 tx_bytes = 0;

    if (_parent->_mergeable_bufs) {
        req->mhdr.num_buffers = 0;
    }

    vqueue->init_sg();
    vqueue->add_out_sg(static_cast<void*>(&req->mhdr), _parent->_hdr_size);

    for (m = m_head; m != NULL; m = m->m_hdr.mh_next) {
        int frag_len = m->m_hdr.mh_len;

        if (frag_len != 0) {
            net_d("Frag len=%d", frag_len);
            tx_bytes += frag_len;
            vqueue->add_out_sg(m->m_hdr.mh_data, m->m_hdr.mh_len);
        }
    }

    req->tx_bytes = tx_bytes;
    vec_sz = vqueue->_sg_vec.size();

    trace_virtio_net_tx_packet_size(vqueue, vec_sz);

    if (!vqueue->add_buf(req)) {
        if (vqueue->used_ring_not_empty()) {
            trace_virtio_net_tx_no_space_calling_gc(_parent->_ifn->if_index);
            gc();
            if (!vqueue->avail_ring_has_room(vec_sz)) {
                req->hw_queue_was_full = 1;
                return ENOBUFS;
            }
        } else {
            req->hw_queue_was_full = 1;
            return ENOBUFS;
        }
    } else {
        return 0;
    }

    if (!vqueue->add_buf(req)) {
        assert(0);
    }

    return 0;
}

inline void net::txq::update_stats(net_req* req)
{
    stats.tx_bytes += req->tx_bytes;
    stats.tx_hw_queue_is_full += req->hw_queue_was_full;
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

        // We are going to poll - flush the pending packets
        kick_pending();
        do {
            if (!vqueue->used_ring_not_empty()) {
                do {
                    using namespace osv::clock::literals;
                    //
                    // The standard default configuration for Tx coalescing
                    // timeout in NICs is about 50us. Wait for twice this time
                    // to ensure that there is at least one Tx interrupt while
                    // we are not running.
                    //
                    sched::thread::yield(100_us);
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

    stats.tx_worker_packets++;
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

// Send VIRTIO_NET_CTRL_MQ_VQ_PAIRS_SET on the control virtqueue to tell the
// device how many Rx/Tx queue pairs the guest will use.  The control queue is
// used only here at init and has no dedicated interrupt thread, so we submit
// the request and poll the used ring synchronously for the device's ACK.
bool net::set_vq_pairs(u16 pairs)
{
    vring* vq = get_virt_queue(_ctrl_vq_idx);
    if (!vq) {
        return false;
    }

    struct {
        net_ctrl_hdr hdr;
        net_ctrl_mq mq;
        net_ctrl_ack ack;
    } cmd;
    cmd.hdr.class_t = VIRTIO_NET_CTRL_MQ;
    cmd.hdr.cmd = VIRTIO_NET_CTRL_MQ_VQ_PAIRS_SET;
    cmd.mq.virtqueue_pairs = pairs;
    cmd.ack = VIRTIO_NET_ERR;

    vq->init_sg();
    vq->add_out_sg(&cmd.hdr, sizeof(cmd.hdr));
    vq->add_out_sg(&cmd.mq, sizeof(cmd.mq));
    vq->add_in_sg(&cmd.ack, sizeof(cmd.ack));

    if (!vq->add_buf(&cmd)) {
        return false;
    }
    vq->kick();

    // The control queue is quiescent at init; busy-wait briefly for the reply.
    u32 len;
    for (int spins = 0; spins < 100000; spins++) {
        if (vq->used_ring_not_empty()) {
            vq->get_buf_elem(&len);
            vq->get_buf_finalize();
            break;
        }
        sched::thread::yield();
    }

    return cmd.ack == VIRTIO_NET_OK;
}

u64 net::get_driver_features()
{
    auto base = virtio_driver::get_driver_features();
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
                 // Multiqueue and its control channel.  VIRTIO_NET_F_MQ lets the
                 // device steer received flows across several Rx queues;
                 // VIRTIO_NET_F_CTRL_VQ is required to send the
                 // CTRL_MQ_VQ_PAIRS_SET command that activates them.
                 | (1 << VIRTIO_NET_F_CTRL_VQ)
                 | (1 << VIRTIO_NET_F_MQ)
            );
}

hw_driver* net::probe(hw_device* dev)
{
    if (auto virtio_dev = dynamic_cast<virtio_device*>(dev)) {
        if (virtio_dev->get_id() == hw_device_id(VIRTIO_VENDOR_ID, VIRTIO_ID_NET)) {
            if (opt_maxnic && maxnic-- <= 0) {
                return nullptr;
            } else {
                return aligned_new<net>(*virtio_dev);
            }
        }
    }

    return nullptr;
}

} // namespace virtio

