/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
/*-
 * Copyright (c) 2013 Tsubai Masanari
 * Copyright (c) 2013 Bryan Venteicher <bryanv@FreeBSD.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * $OpenBSD: src/sys/dev/pci/if_vmx.c,v 1.11 2013/06/22 00:28:10 uebayasi Exp $
 */


#include <sys/cdefs.h>

#include "drivers/vmxnet3.hh"
#include "drivers/pci-device.hh"
#include <osv/interrupt.hh>

#include <sstream>
#include <string>
#include <string.h>
#include <map>
#include <errno.h>
#include <osv/debug.h>

#include <osv/sched.hh>
#include <osv/trace.hh>

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
#include <bsd/x64/machine/atomic.h>
#include <bsd/x64/machine/in_cksum.h>
#include <typeinfo>
#include <cxxabi.h>

using namespace memory;

namespace vmw {

int vmxnet3::_instance = 0;

#define vmxnet3_tag "vmxnet3"
#define vmxnet3_d(...)   tprintf_d(vmxnet3_tag, __VA_ARGS__)
#define vmxnet3_i(...)   tprintf_i(vmxnet3_tag, __VA_ARGS__)
#define vmxnet3_w(...)   tprintf_w(vmxnet3_tag, __VA_ARGS__)
#define vmxnet3_e(...)   tprintf_e(vmxnet3_tag, __VA_ARGS__)

static int if_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
    vmxnet3_d("if_ioctl %x", command);

    int error = 0;
    switch(command) {
    case SIOCSIFFLAGS:
        vmxnet3_d("SIOCSIFFLAGS");
        /* Change status ifup, ifdown */
        if (ifp->if_flags & IFF_UP) {
            ifp->if_drv_flags |= IFF_DRV_RUNNING;
            vmxnet3_d("if_up");
        } else {
            ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
            vmxnet3_d("if_down");
        }
        break;
    case SIOCADDMULTI:
    case SIOCDELMULTI:
        vmxnet3_d("SIOCDELMULTI");
        break;
    default:
        vmxnet3_d("redirecting to ether_ioctl()...");
        error = ether_ioctl(ifp, command, data);
        break;
    }

    return(error);
}

/**
 * Invalidate the local Tx queues.
 * @param ifp upper layer instance handle
 */
static void if_qflush(struct ifnet *ifp)
{
    /*
     * Since vmxnet3 currently doesn't have any Tx queue we just
     * flush the upper layer queues.
     */
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
    vmxnet3* vmx = (vmxnet3*)ifp->if_softc;
    vmxnet3_d("%s_start", __FUNCTION__);
    vmxnet3_d("*** processing packet! ***");
    int error = vmx->transmit(m_head);

    return error;
}

static void if_init(void* xsc)
{
    vmxnet3_d("vmxnet3 init");
}

/**
 * Return all the statistics we have gathered.
 * @param ifp
 * @param out_data
 */
static void if_getinfo(struct ifnet* ifp, struct if_data* out_data)
{
    vmxnet3* vmx = (vmxnet3*)ifp->if_softc;

    // First - take the ifnet data
    memcpy(out_data, &ifp->if_data, sizeof(*out_data));

    // then fill the internal statistics we've gathered
    vmx->fill_stats(out_data);
}

void vmxnet3::fill_stats(struct if_data* out_data) const
{
    assert(!out_data->ifi_oerrors && !out_data->ifi_obytes && !out_data->ifi_opackets);
    out_data->ifi_ipackets += _rxq[0].stats.rx_packets;
    out_data->ifi_ibytes   += _rxq[0].stats.rx_bytes;
    out_data->ifi_iqdrops  += _rxq[0].stats.rx_drops;
    out_data->ifi_ierrors  += _rxq[0].stats.rx_csum_err;
    out_data->ifi_opackets += _txq_stats.tx_packets;
    out_data->ifi_obytes   += _txq_stats.tx_bytes;
    out_data->ifi_oerrors  += _txq_stats.tx_err + _txq_stats.tx_drops;
}

void vmxnet3_txqueue::init()
{
    layout->cmd_ring = cmd_ring.get_desc_pa();
    layout->cmd_ring_len = cmd_ring.get_desc_num();
    layout->comp_ring = comp_ring.get_desc_pa();
    layout->comp_ring_len = comp_ring.get_desc_num();

    layout->driver_data = mmu::virt_to_phys(this);
    layout->driver_data_len = sizeof(*this);

    auto &txr = cmd_ring;
    txr.head = 0;
    txr.next = 0;
    txr.gen = init_gen;
    txr.clear_descs();

    auto &txc = comp_ring;
    txc.next = 0;
    txc.gen = init_gen;
    txc.clear_descs();
}

void vmxnet3_rxqueue::init(struct ifnet* ifn, pci::bar *bar0)
{
    _ifn = ifn;
    _bar0 = bar0;
    for (unsigned i = 0; i < VMXNET3_RXRINGS_PERQ; i++) {
        layout->cmd_ring[i] = _cmd_rings[i].get_desc_pa();
        layout->cmd_ring_len[i] = _cmd_rings[i].get_desc_num();
    }

    layout->comp_ring = _comp_ring.get_desc_pa();
    layout->comp_ring_len = _comp_ring.get_desc_num();
    layout->driver_data = mmu::virt_to_phys(this);
    layout->driver_data_len = sizeof(*this);

    for (unsigned i = 0; i < VMXNET3_RXRINGS_PERQ; i++) {
        auto &rxr = _cmd_rings[i];
        rxr.fill = 0;
        rxr.gen = init_gen;
        rxr.clear_descs();

        for (unsigned idx = 0; idx < rxr.get_desc_num(); idx++) {
            newbuf(i);
        }
    }

    auto &rxc = _comp_ring;
    rxc.next = 0;
    rxc.gen = init_gen;
    rxc.clear_descs();

    task.start();
}

void vmxnet3_rxqueue::discard(int rid, int idx)
{
    auto &rxr = _cmd_rings[rid];
    auto rxd = rxr.get_desc(idx);
    rxd->layout->gen = rxr.gen;
    rxr.increment_fill();
}

void vmxnet3_rxqueue::newbuf(int rid)
{
    auto &rxr = _cmd_rings[rid];
    auto idx = rxr.fill;
    auto rxd = rxr.get_desc(idx);
    int flags, clsize, type;

    if (rid == 0 && (idx % 1) == 0) {
        flags = M_PKTHDR;
        clsize = MJUM16BYTES;
        type = btype::head;
    } else {
        flags = 0;
        clsize = MJUM16BYTES;
        type = btype::body;
    }
    auto m = m_getjcl(M_NOWAIT, MT_DATA, flags, clsize);
    if (m == NULL) {
        panic("mbuf allocation failed");
        return;
    }
    if (type == btype::head) {
        m->m_hdr.mh_len = m->M_dat.MH.MH_pkthdr.len = clsize;
        m_adj(m, ETHER_ALIGN);
    }else
        m->m_hdr.mh_len = clsize;

    _buf[rid][idx] = m;

    rxd->layout->addr = mmu::virt_to_phys(m->m_hdr.mh_data);
    rxd->layout->len = std::min(static_cast<u32>(m->m_hdr.mh_len),
                                static_cast<u32>(VMXNET3_MAX_DESC_LEN));
    rxd->layout->btype = type;
    rxd->layout->gen = rxr.gen;

    rxr.increment_fill();
}


vmxnet3::vmxnet3(pci::device &dev)
    : _dev(dev)
    , _msi(&dev)
    , _drv_shared_mem(vmxnet3_drv_shared::size(),
                        align::driver_shared)
    , _queues_shared_mem(vmxnet3_txq_shared::size() * tx_queues +
                            vmxnet3_rxq_shared::size() * rx_queues,
                            align::queues_shared)
    , _mcast_list(multicast_max * eth_alen, align::multicast)
    , _xmit_it(this)
    , _xmitter(this)
    , _worker([this] { _xmitter.poll_until([] { return false; }, _xmit_it); })
{
    u_int8_t macaddr[6];

    parse_pci_config();
    _dev.set_bus_master(true);
    _dev.msix_enable();
    assert(dev.is_msix());
    disable_interrupts();
    stop();
    vmxnet3_i("VMXNET3 INSTANCE");
    _id = _instance++;
    _drv_shared.attach(_drv_shared_mem.get_va());

    //initialize the BSD interface _if
    _ifn = if_alloc(IFT_ETHER);
    if (_ifn == NULL) {
       //FIXME: need to handle this case - expand the above function not to allocate memory and
       // do it within the constructor.
       vmxnet3_w("if_alloc failed!");
       return;
    }

    attach_queues_shared(_ifn, _bar0);

    do_version_handshake();
    allocate_interrupts();
    fill_driver_shared();

    enable_device();

    dump_config();

    if_initname(_ifn, "eth", _id);
    _ifn->if_mtu = ETHERMTU;
    _ifn->if_softc = static_cast<void*>(this);
    _ifn->if_flags = IFF_BROADCAST | IFF_MULTICAST;
    _ifn->if_ioctl = if_ioctl;
    _ifn->if_transmit = if_transmit;
    _ifn->if_qflush = if_qflush;
    _ifn->if_init = if_init;
    _ifn->if_getinfo = if_getinfo;
    IFQ_SET_MAXLEN(&_ifn->if_snd, VMXNET3_MAX_TX_NDESC);


    _ifn->if_capabilities = IFCAP_RXCSUM | IFCAP_TXCSUM;
    _ifn->if_capabilities |= IFCAP_TSO4;
    _ifn->if_capabilities |= IFCAP_LRO;
    _ifn->if_hwassist = CSUM_TCP | CSUM_UDP | CSUM_TSO;
    _ifn->if_capenable = _ifn->if_capabilities | IFCAP_HWSTATS;

    get_mac_address(macaddr);
    ether_ifattach(_ifn, macaddr);
    _worker.start();
    enable_interrupts();

    _zone_req = uma_zcreate("vmxnet3_req", sizeof(vmxnet3_req), NULL, NULL, NULL,
        NULL, UMA_ALIGN_PTR, UMA_ZONE_MAXBUCKET);
}

void vmxnet3::dump_config(void)
{
    u8 B, D, F;
    _dev.get_bdf(B, D, F);

    _dev.dump_config();
    vmxnet3_d("%s [%x:%x.%x] vid:id= %x:%x", get_name().c_str(),
        (u16)B, (u16)D, (u16)F,
        _dev.get_vendor_id(),
        _dev.get_device_id());
}

void vmxnet3::allocate_interrupts()
{
    _msi.easy_register({
        { 0, [] {}, nullptr },
        { 1, [] {}, &_rxq[0].task }
    });
    _txq[0].layout->intr_idx = 0;
    _rxq[0].set_intr_idx(1);
}

void vmxnet3::enable_interrupts()
{
    enable_interrupt(1);
}

void vmxnet3::enable_interrupt(unsigned idx)
{
    _bar0->writel(bar0_imask(idx), 0);
}

void vmxnet3::disable_interrupts()
{
    for (unsigned idx = 0; idx < num_intrs; idx++)
        disable_interrupt(idx);
}

void vmxnet3::disable_interrupt(unsigned idx)
{
    _bar0->writel(bar0_imask(idx), 1);
}

void vmxnet3::attach_queues_shared(struct ifnet* ifn, pci::bar *bar0)
{
    auto *va = _queues_shared_mem.get_va();

    slice_memory(va, _txq);
    slice_memory(va, _rxq);

    for (auto &q : _txq) {
        q.init();
    }
    for (auto &q : _rxq) {
        q.init(ifn, bar0);
    }
}

void vmxnet3::fill_driver_shared()
{
    _drv_shared.set_driver_data(mmu::virt_to_phys(this), sizeof(*this));
    _drv_shared.set_queue_shared(_queues_shared_mem.get_pa(),
                                 _queues_shared_mem.get_size());
    _drv_shared.set_max_sg_len(max_rx_segs);
    _drv_shared.set_mcast_table(_mcast_list.get_pa(),
                                _mcast_list.get_size());
    _drv_shared.set_intr_config(2, 0);
    _drv_shared.layout->upt_features = upt1::fcsum | upt1::flro;
    _drv_shared.layout->mtu = 1500;
    _drv_shared.layout->ntxqueue = 1;
    _drv_shared.layout->nrxqueue = 1;
    _drv_shared.layout->rxmode = rxmode::ucast | rxmode::bcast | rxmode::allmulti | rxmode::mcast;
    _bar1->writel(bar1::dsl, _drv_shared_mem.get_pa());
    _bar1->writel(bar1::dsh,
        reinterpret_cast<u64>(_drv_shared_mem.get_pa()) >> 32);
    write_cmd(command::set_filter);
    write_cmd(command::set_rxmode);
}

hw_driver* vmxnet3::probe(hw_device* dev)
{
    try {
        if (auto pci_dev = dynamic_cast<pci::device*>(dev)) {
            pci_dev->dump_config();
            if (pci_dev->get_id() ==
                hw_device_id(pciconf::vendor_id, pciconf::device_id)) {
                return new vmxnet3(*pci_dev);
            }
        }
    } catch (std::exception& e) {
        vmxnet3_e("Exception on device construction: %s", e.what());
    }
    return nullptr;
}

void vmxnet3::parse_pci_config()
{
    if (!_dev.parse_pci_config()) {
        throw std::runtime_error("_dev cannot parse PCI config");
    }
    _bar0 = _dev.get_bar(1);
    _bar0->map();
    if (_bar0 == nullptr) {
        throw std::runtime_error("BAR1 is absent");
    }

    _bar1 = _dev.get_bar(2);
    _bar1->map();
    if (_bar1 == nullptr) {
        throw std::runtime_error("BAR2 is absent");
    }
}

void vmxnet3::stop()
{
    write_cmd(command::disable);
    write_cmd(command::reset);
}

void vmxnet3::enable_device()
{
    read_cmd(command::enable);
    _bar0->writel(bar0::rxh1, 0);
    _bar0->writel(bar0::rxh2, 0);
}

void vmxnet3::do_version_handshake()
{
    auto val = _bar1->readl(bar1::vrrs);
    if ((val & VMXNET3_VERSIONS_MASK) != VMXNET3_REVISION) {
        auto err = boost::format("unknown HW version %d") % val;
        throw std::runtime_error(err.str());
    }
    _bar1->writel(bar1::vrrs, VMXNET3_REVISION);

    val = _bar1->readl(bar1::uvrs);
    if ((val & VMXNET3_VERSIONS_MASK) != VMXNET3_UPT_VERSION) {
        auto err = boost::format("unknown UPT version %d") % val;
        throw std::runtime_error(err.str());
    }
    _bar1->writel(bar1::uvrs, VMXNET3_UPT_VERSION);
}

void vmxnet3::write_cmd(u32 cmd)
{
    _bar1->writel(bar1::cmd, cmd);
}

u32 vmxnet3::read_cmd(u32 cmd)
{
    write_cmd(cmd);
    mb();
    return _bar1->readl(bar1::cmd);
}

int vmxnet3::transmit(struct mbuf *m_head)
{
    return _xmitter.xmit(m_head);
}

int vmxnet3::xmit_prep(mbuf* m_head, void*& cooky)
{
    unsigned count = 0;
    auto req = static_cast<vmxnet3_req *>(uma_zalloc(_zone_req, M_NOWAIT));
    if (!req) {
        return ENOMEM;
    }
    for (auto m = m_head; m != NULL; m = m->m_hdr.mh_next)
        count++;
    req->mb = m_head;
    req->count = count;

    if (m_head->M_dat.MH.MH_pkthdr.csum_flags
        & (CSUM_TCP | CSUM_UDP | CSUM_TSO)) {
        int error = txq_offload(req);
        if (error) {
            uma_zfree(_zone_req, req);
            return error;
        }
    }
    cooky = req;
    return 0;
}

int vmxnet3::try_xmit_one_locked(void *req)
{
    auto _req = static_cast<vmxnet3_req *>(req);
    return try_xmit_one_locked(_req);
}

int vmxnet3::try_xmit_one_locked(vmxnet3_req *req)
{
    auto count = req->count;
    if (_txq[0].avail < count) {
        txq_gc(_txq[0]);
        if (_txq[0].avail < count)
            return ENOBUFS;
    }
    txq_encap(_txq[0], req);
    uma_zfree(_zone_req, req);
    return 0;
}

void vmxnet3::xmit_one_locked(void *req)
{
    auto _req = static_cast<vmxnet3_req *>(req);
    if (try_xmit_one_locked(_req)) {
        kick_pending();
        do {
            sched::thread::yield();
        } while (try_xmit_one_locked(_req));
    }

    //
    // The packet has been posted - increase the counter of a "pending for a kick"
    // packets.
    //
    ++_txq[0].layout->npending;
}

void vmxnet3::kick_pending()
{
    if (_txq[0].layout->npending)
        kick_hw();
}

void vmxnet3::kick_pending_with_thresh()
{
    if (_txq[0].layout->npending >= _txq[0].layout->intr_threshold)
        kick_hw();
}

bool vmxnet3::kick_hw()
{
    auto &txr = _txq[0].cmd_ring;

    _txq[0].layout->npending = 0;
    _bar0->writel(bar0::txh, txr.head);
    return true;
}

void vmxnet3::wake_worker()
{
    _worker.wake();
}

void vmxnet3::txq_encap(vmxnet3_txqueue &txq, vmxnet3_req *req)
{
    auto &txr = txq.cmd_ring;
    auto txd = txr.get_desc(txr.head);
    auto sop = txr.get_desc(txr.head);
    auto gen = txr.gen ^ 1; // Owned by cpu (yet)
    u64 tx_bytes = 0;
    auto m_head = req->mb;
    auto start = req->start;

    assert(txq.buf[txr.head] == NULL);
    txq.buf[txr.head] = m_head;
    for (auto m = m_head; m != NULL; m = m->m_hdr.mh_next) {
        int frag_len = m->m_hdr.mh_len;
        vmxnet3_d("Frag len=%d:", frag_len);
        tx_bytes += frag_len;
        --txq.avail;
        txd = txr.get_desc(txr.head);
        txd->layout->addr = mmu::virt_to_phys(m->m_hdr.mh_data);
        txd->layout->len = frag_len;
        txd->layout->gen = gen;
        txd->layout->dtype = 0;
        txd->layout->offload_mode = om::none;
        txd->layout->offload_pos = 0;
        txd->layout->hlen = 0;
        txd->layout->eop = 0;
        txd->layout->compreq = 0;
        txd->layout->vtag_mode = 0;
        txd->layout->vtag = 0;

        if (++txr.head == txr.get_desc_num()) {
            txr.head = 0;
            txr.gen ^= 1;
        }
        gen = txr.gen;
    }
    txd->layout->eop = 1;
    txd->layout->compreq = 1;

    if (m_head->m_hdr.mh_flags & M_VLANTAG) {
        sop->layout->vtag_mode = 1;
        sop->layout->vtag = m_head->M_dat.MH.MH_pkthdr.ether_vtag;
    }

    if (m_head->M_dat.MH.MH_pkthdr.csum_flags & CSUM_TSO) {
        sop->layout->offload_mode = om::tso;
        sop->layout->hlen = start;
        sop->layout->offload_pos = m_head->M_dat.MH.MH_pkthdr.tso_segsz;
    } else if (m_head->M_dat.MH.MH_pkthdr.csum_flags & (CSUM_TCP | CSUM_UDP)) {
        sop->layout->offload_mode = om::csum;
        sop->layout->hlen = start;
        sop->layout->offload_pos = start + m_head->M_dat.MH.MH_pkthdr.csum_data;
    }

    // Finally, change the ownership.
    wmb();
    sop->layout->gen ^= 1;

    _txq_stats.tx_bytes += tx_bytes;
    _txq_stats.tx_packets++;
}

int vmxnet3::txq_offload(vmxnet3_req *req)
{
    struct ether_vlan_header *evh;
    int offset;
    int etype, proto, start;
    auto m = req->mb;
    struct ip *ip, iphdr;

    evh = mtod(m, struct ether_vlan_header *);
    if (evh->evl_encap_proto == htons(ETHERTYPE_VLAN)) {
        /* BMV: We should handle nested VLAN tags too. */
        etype = ntohs(evh->evl_proto);
        offset = sizeof(struct ether_vlan_header);
    } else {
        etype = ntohs(evh->evl_encap_proto);
        offset = sizeof(struct ether_header);
    }

    switch (etype) {
    case ETHERTYPE_IP: {
        if (__predict_false(m->m_hdr.mh_len < offset + static_cast<int>(sizeof(struct ip)))) {
            m_copydata(m, offset, sizeof(struct ip),
                (caddr_t) &iphdr);
            ip = &iphdr;
        } else
            ip = (struct ip *)(m->m_hdr.mh_data + offset);
        proto = ip->ip_p;
        start = offset + (ip->ip_hl << 2);
        break;
    }
    default:
        return (EINVAL);
    }

    if (m->M_dat.MH.MH_pkthdr.csum_flags & CSUM_TSO) {
        struct tcphdr *tcp, tcphdr;
        uint16_t sum;

        if (__predict_false(proto != IPPROTO_TCP)) {
            /* Likely failed to correctly parse the mbuf. */
            return (EINVAL);
        }

        switch (etype) {
            case ETHERTYPE_IP:
                sum = in_pseudo(ip->ip_src.s_addr, ip->ip_dst.s_addr,
                    htons(IPPROTO_TCP));
                break;
            default:
                sum = 0;
                break;
        }

        if (m->m_hdr.mh_len < start + static_cast<int>(sizeof(struct tcphdr))) {
            m_copyback(m, start + offsetof(struct tcphdr, th_sum),
                sizeof(uint16_t), (caddr_t) &sum);
            m_copydata(m, start, sizeof(struct tcphdr),
                (caddr_t) &tcphdr);
            tcp = &tcphdr;
        } else {
            tcp = (struct tcphdr *)(m->m_hdr.mh_data + start);
            tcp->th_sum = sum;
        }

        /*
         * For TSO, the size of the protocol header is also
         * included in the descriptor header size.
         */
        start += (tcp->th_off << 2);
    }
    req->start = start;

    return (0);
}

void vmxnet3::txq_gc(vmxnet3_txqueue &txq)
{
    auto &txr = txq.cmd_ring;
    auto &txc = txq.comp_ring;
    while(1) {
        auto txcd = txc.get_desc(txc.next);
        if (txcd->layout->gen != txc.gen)
            break;
        rmb();
        if (++txc.next == txc.get_desc_num()) {
            txc.next = 0;
            txc.gen ^= 1;
        }

        auto sop = txr.next;
        auto m_head = txq.buf[sop];

        if (m_head != NULL) {
            int count = 0;

            for (auto m = m_head; m != NULL;) {
                auto m_next = m->m_hdr.mh_next;
                ++count;
                m_free(m);
                m = m_next;
            }
            txq.buf[sop] = NULL;
            txq.avail += count;
        }

        txr.next =
            (txcd->layout->eop_idx + 1 ) % txr.get_desc_num();
    }
}

void vmxnet3_rxqueue::receive_work()
{
    while(1) {
        enable_interrupt();
        sched::thread::wait_until([&] {
            return available();
        });
        disable_interrupt();
        do {
            receive();
        } while(available());
    }
}

void vmxnet3_rxqueue::receive()
{
    auto &rxc = _comp_ring;

    while(1) {
        auto rxcd = rxc.get_desc(rxc.next);
        assert(rxcd->layout->qid <= 2);

        if (rxcd->layout->gen != rxc.gen)
            break;
        rmb();

        if (++rxc.next == rxc.get_desc_num()) {
            rxc.next = 0;
            rxc.gen ^= 1;
        }

        auto rid = rxcd->layout->qid;
        auto idx = rxcd->layout->rxd_idx;
        auto length = rxcd->layout->len;
        auto &rxr = _cmd_rings[rid];
        auto rxd = rxr.get_desc(idx);
        auto m = _buf[rid][idx];

        assert(m != NULL);

        if (rxr.fill != idx) {
            while(rxr.fill != idx) {
                rxr.get_desc(rxr.fill)->layout->gen = rxr.gen;
                rxr.increment_fill();
            }
        }

        if (rxcd->layout->sop) {
            assert(rxd->layout->btype == btype::head);
            assert((idx % 1) == 0);
            assert(_m_currpkt_head == nullptr);

            if (length == 0) {
                discard(rid, idx);
                goto next;
            }

            newbuf(rid);

            m->M_dat.MH.MH_pkthdr.len = length;
            m->M_dat.MH.MH_pkthdr.rcvif = _ifn;
            m->M_dat.MH.MH_pkthdr.csum_flags = 0;
            m->m_hdr.mh_len = length;
            _m_currpkt_head = _m_currpkt_tail = m;
        } else {
            assert(rxd->layout->btype == btype::body);
            assert(_m_currpkt_head != nullptr);

            newbuf(rid);

            m->m_hdr.mh_len = length;
            _m_currpkt_head->M_dat.MH.MH_pkthdr.len += length;
            _m_currpkt_tail->m_hdr.mh_next = m;
            _m_currpkt_tail = m;
        }

        if (rxcd->layout->eop) {
            input(rxcd, _m_currpkt_head);
            _m_currpkt_head = _m_currpkt_tail = nullptr;
        }

next:
        if (layout->update_rxhead) {
            idx = (idx + 1) % rxr.get_desc_num();
            if (rid == 0)
                _bar0->writel(bar0::rxh1, idx);
            else
                _bar0->writel(bar0::rxh2, idx);
        }
    }
}

bool vmxnet3_rxqueue::available()
{
    auto &rxc = _comp_ring;
    auto rxcd = rxc.get_desc(rxc.next);
    assert(rxcd->layout->qid <= 2);

    return (rxcd->layout->gen == rxc.gen);
}

void vmxnet3_rxqueue::checksum(vmxnet3_rx_compdesc *rxcd, struct mbuf *m)
{
    if (rxcd->layout->ipv4) {
        m->M_dat.MH.MH_pkthdr.csum_flags |= CSUM_IP_CHECKED;
        if (rxcd->layout->ipcsum_ok)
            m->M_dat.MH.MH_pkthdr.csum_flags |= CSUM_IP_VALID;
    }
    if (!rxcd->layout->fragment) {
        if (rxcd->layout->csum_ok &&
            (rxcd->layout->tcp || rxcd->layout->udp)) {
            m->M_dat.MH.MH_pkthdr.csum_flags |= CSUM_DATA_VALID
                | CSUM_PSEUDO_HDR;
            m->M_dat.MH.MH_pkthdr.csum_data = 0xffff;
        }
    }
}

void vmxnet3_rxqueue::input(vmxnet3_rx_compdesc *rxcd, struct mbuf *m)

{
    if (rxcd->layout->error) {
        m_freem(m);
        stats.rx_csum_err++;
        return;
    }
    if (!rxcd->layout->no_csum)
        checksum(rxcd, m);
    stats.rx_packets++;
    stats.rx_bytes += m->M_dat.MH.MH_pkthdr.len;
    bool fast_path = _ifn->if_classifier.post_packet(m);
    if (!fast_path) {
        (*_ifn->if_input)(_ifn, m);
    }
}

void vmxnet3_rxqueue::enable_interrupt()
{
    _bar0->writel(bar0_imask(layout->intr_idx), 0);
}

void vmxnet3_rxqueue::disable_interrupt()
{
    _bar0->writel(bar0_imask(layout->intr_idx), 1);
}

void vmxnet3::get_mac_address(u_int8_t *macaddr)
{
    auto macl = read_cmd(command::get_macl);
    auto mach = read_cmd(command::get_mach);
    macaddr[0] = macl;
    macaddr[1] = macl >> 8;
    macaddr[2] = macl >> 16;
    macaddr[3] = macl >> 24;
    macaddr[4] = mach;
    macaddr[5] = mach >> 8;
    vmxnet3_i("The mac addr of the device is %x:%x:%x:%x:%x:%x",
            (u32)macaddr[0],
            (u32)macaddr[1],
            (u32)macaddr[2],
            (u32)macaddr[3],
            (u32)macaddr[4],
            (u32)macaddr[5]);
}

template<class DescT, int NDesc>
void vmxnet3_ring<DescT, NDesc>::increment_fill()
{
    if (++fill == get_desc_num()) {
        fill = 0;
        gen ^= 1;
    }
}

}
