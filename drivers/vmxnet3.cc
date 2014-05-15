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
    case SIOCSIFMTU:
        vmxnet3_d("SIOCSIFMTU");
        break;
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
    out_data->ifi_ipackets += _rxq_stats.rx_packets;
    out_data->ifi_ibytes   += _rxq_stats.rx_bytes;
    out_data->ifi_iqdrops  += _rxq_stats.rx_drops;
    out_data->ifi_ierrors  += _rxq_stats.rx_csum_err;
    out_data->ifi_opackets += _txq_stats.tx_packets;
    out_data->ifi_obytes   += _txq_stats.tx_bytes;
    out_data->ifi_oerrors  += _txq_stats.tx_err + _txq_stats.tx_drops;
}

/**
 * Initialize an array of containers with specific virtual address.
 * Takes Preallocated buffer address and splits it into chunks of required size,
 * associates each chunk with an array element.
 * @param va preallocated buffer address
 * @param holder array of containers
 */
template<class T> void slice_memory(void *&va, T &holder)
{
    for (auto &e : holder) {
        e.attach(va);
        va += e.size();
    }
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
    txr.gen = vmxnet3::VMXNET3_INIT_GEN;
    txr.clear_descs();

    auto &txc = comp_ring;
    txc.next = 0;
    txc.gen = vmxnet3::VMXNET3_INIT_GEN;
    txc.clear_descs();
}

void vmxnet3_rxqueue::init()
{
    for (unsigned i = 0; i < VMXNET3_RXRINGS_PERQ; i++) {
        layout->cmd_ring[i] = cmd_rings[i].get_desc_pa();
        layout->cmd_ring_len[i] = cmd_rings[i].get_desc_num();
    }

    layout->comp_ring = comp_ring.get_desc_pa();
    layout->comp_ring_len = comp_ring.get_desc_num();
    layout->driver_data = mmu::virt_to_phys(this);
    layout->driver_data_len = sizeof(*this);

    for (unsigned i = 0; i < VMXNET3_RXRINGS_PERQ; i++) {
        auto &rxr = cmd_rings[i];
        rxr.fill = 0;
        rxr.gen = vmxnet3::VMXNET3_INIT_GEN;
        rxr.clear_descs();

        for (unsigned idx = 0; idx < rxr.get_desc_num(); idx++) {
            newbuf(i);
        }
    }

    auto &rxc = comp_ring;
    rxc.next = 0;
    rxc.gen = vmxnet3::VMXNET3_INIT_GEN;
    rxc.clear_descs();
}

void vmxnet3_rxqueue::discard(int rid, int idx)
{
    auto &rxr = cmd_rings[rid];
    auto rxd = rxr.get_desc(idx);
    rxd->layout->gen = rxr.gen;
    rxr.increment_fill();
}

void vmxnet3_rxqueue::newbuf(int rid)
{
    auto &rxr = cmd_rings[rid];
    auto idx = rxr.fill;
    auto rxd = rxr.get_desc(idx);
    int flags, clsize, btype;

    if (rid == 0 && (idx % 1) == 0) {
        flags = M_PKTHDR;
        clsize = MJUM16BYTES;
        btype = vmxnet3::VMXNET3_BTYPE_HEAD;
    } else {
        flags = 0;
        clsize = MJUM16BYTES;
        btype = vmxnet3::VMXNET3_BTYPE_BODY;
    }
    auto m = m_getjcl(M_NOWAIT, MT_DATA, flags, clsize);
    if (m == NULL) {
        panic("mbuf allocation failed");
        return;
    }
    if (btype == vmxnet3::VMXNET3_BTYPE_HEAD) {
        m->m_hdr.mh_len = m->M_dat.MH.MH_pkthdr.len = clsize;
        m_adj(m, ETHER_ALIGN);
    }else
        m->m_hdr.mh_len = clsize;

    buf[rid][idx] = m;

    rxd->layout->addr = mmu::virt_to_phys(m->m_hdr.mh_data);
    rxd->layout->len = m->m_hdr.mh_len;
    rxd->layout->btype = btype;
    rxd->layout->gen = rxr.gen;

    rxr.increment_fill();
}


vmxnet3::vmxnet3(pci::device &dev)
    : _dev(dev)
    , _msi(&dev)
    , _drv_shared_mem(vmxnet3_drv_shared::size(),
                        VMXNET3_DRIVER_SHARED_ALIGN)
    , _queues_shared_mem(vmxnet3_txq_shared::size() * VMXNET3_TX_QUEUES +
                            vmxnet3_rxq_shared::size() * VMXNET3_RX_QUEUES,
                            VMXNET3_QUEUES_SHARED_ALIGN)
    , _mcast_list(VMXNET3_MULTICAST_MAX * VMXNET3_ETH_ALEN, VMXNET3_MULTICAST_ALIGN)
    , _receive_task([&] { receive_work(); }, sched::thread::attr().name("vmxnet3-receive"))
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
    attach_queues_shared();

    do_version_handshake();
    allocate_interrupts();
    fill_driver_shared();

    enable_device();

    dump_config();

    //initialize the BSD interface _if
    _ifn = if_alloc(IFT_ETHER);
    if (_ifn == NULL) {
       //FIXME: need to handle this case - expand the above function not to allocate memory and
       // do it within the constructor.
       vmxnet3_w("if_alloc failed!");
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
    IFQ_SET_MAXLEN(&_ifn->if_snd, VMXNET3_MAX_TX_NDESC);


    _ifn->if_capabilities = IFCAP_RXCSUM | IFCAP_TXCSUM;
    _ifn->if_capabilities |= IFCAP_TSO4;
    _ifn->if_capabilities |= IFCAP_LRO;
    _ifn->if_hwassist = CSUM_TCP | CSUM_UDP | CSUM_TSO;
    _ifn->if_capenable = _ifn->if_capabilities | IFCAP_HWSTATS;

    get_mac_address(macaddr);
    ether_ifattach(_ifn, macaddr);
    _receive_task.start();
    enable_interrupts();
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
        { 1, [] {}, &_receive_task }
    });
    _txq[0].layout->intr_idx = 0;
    _rxq[0].layout->intr_idx = 1;
}

void vmxnet3::enable_interrupts()
{
    enable_interrupt(1);
}

void vmxnet3::enable_interrupt(unsigned idx)
{
    _bar0->writel(VMXNET3_BAR0_IMASK(idx), 0);
}

void vmxnet3::disable_interrupts()
{
    for (unsigned idx = 0; idx < VMXNET3_NUM_INTRS; idx++)
        disable_interrupt(idx);
}

void vmxnet3::disable_interrupt(unsigned idx)
{
    _bar0->writel(VMXNET3_BAR0_IMASK(idx), 1);
}

void vmxnet3::attach_queues_shared()
{
    auto *va = _queues_shared_mem.get_va();

    slice_memory(va, _txq);
    slice_memory(va, _rxq);

    for (auto &q : _txq) {
        q.init();
    }
    for (auto &q : _rxq) {
        q.init();
    }
}

void vmxnet3::fill_driver_shared()
{
    _drv_shared.set_driver_data(mmu::virt_to_phys(this), sizeof(*this));
    _drv_shared.set_queue_shared(_queues_shared_mem.get_pa(),
                                 _queues_shared_mem.get_size());
    _drv_shared.set_max_sg_len(VMXNET3_MAX_RX_SEGS);
    _drv_shared.set_mcast_table(_mcast_list.get_pa(),
                                _mcast_list.get_size());
    _drv_shared.set_intr_config(2, 0);
    _drv_shared.layout->upt_features = UPT1_F_CSUM | UPT1_F_LRO;
    _drv_shared.layout->mtu = 1500;
    _drv_shared.layout->ntxqueue = 1;
    _drv_shared.layout->nrxqueue = 1;
    _drv_shared.layout->rxmode = VMXNET3_RXMODE_UCAST | VMXNET3_RXMODE_BCAST | VMXNET3_RXMODE_ALLMULTI | VMXNET3_RXMODE_MCAST;
    _bar1->writel(VMXNET3_BAR1_DSL, _drv_shared_mem.get_pa());
    _bar1->writel(VMXNET3_BAR1_DSH,
        reinterpret_cast<u64>(_drv_shared_mem.get_pa()) >> 32);
    write_cmd(VMXNET3_CMD_SET_FILTER);
    write_cmd(VMXNET3_CMD_SET_RXMODE);
}

hw_driver* vmxnet3::probe(hw_device* dev)
{
    try {
        if (auto pci_dev = dynamic_cast<pci::device*>(dev)) {
            pci_dev->dump_config();
            if (pci_dev->get_id() ==
                hw_device_id(VMXNET3_VENDOR_ID, VMXNET3_DEVICE_ID)) {
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
    write_cmd(VMXNET3_CMD_DISABLE);
    write_cmd(VMXNET3_CMD_RESET);
}

void vmxnet3::enable_device()
{
    read_cmd(VMXNET3_CMD_ENABLE);
    _bar0->writel(VMXNET3_BAR0_RXH1, 0);
    _bar0->writel(VMXNET3_BAR0_RXH2, 0);
}

void vmxnet3::do_version_handshake()
{
    auto val = _bar1->readl(VMXNET3_BAR1_VRRS);
    if ((val & VMXNET3_VERSIONS_MASK) != VMXNET3_REVISION) {
        auto err = boost::format("unknown HW version %d") % val;
        throw std::runtime_error(err.str());
    }
    _bar1->writel(VMXNET3_BAR1_VRRS, VMXNET3_REVISION);

    val = _bar1->readl(VMXNET3_BAR1_UVRS);
    if ((val & VMXNET3_VERSIONS_MASK) != VMXNET3_UPT_VERSION) {
        auto err = boost::format("unknown UPT version %d") % val;
        throw std::runtime_error(err.str());
    }
    _bar1->writel(VMXNET3_BAR1_UVRS, VMXNET3_UPT_VERSION);
}

void vmxnet3::write_cmd(u32 cmd)
{
    _bar1->writel(VMXNET3_BAR1_CMD, cmd);
}

u32 vmxnet3::read_cmd(u32 cmd)
{
    write_cmd(cmd);
    mb();
    return _bar1->readl(VMXNET3_BAR1_CMD);
}

int vmxnet3::transmit(struct mbuf *m_head)
{
    int error;
    WITH_LOCK(_txq_lock) {
        int count = 0;
        for (auto m = m_head; m != NULL; m = m->m_hdr.mh_next)
            ++count;
        if (_txq[0].avail < count) {
            txq_gc(_txq[0]);
            if (_txq[0].avail < count) {
                vmxnet3_d("%s: no room", __FUNCTION__);
                m_freem(m_head);
                _txq_stats.tx_drops++;
                return ENOBUFS;
            }
        }
        error = txq_encap(_txq[0], m_head);
    }
    return error;
}

void vmxnet3::receive_work()
{
    while(1) {
        enable_interrupt(1);
        sched::thread::wait_until([&] {
            return rxq_avail(_rxq[0]);
        });
        disable_interrupt(1);
        do {
            rxq_eof(_rxq[0]);
        } while(rxq_avail(_rxq[0]));
    }
}

int vmxnet3::txq_encap(vmxnet3_txqueue &txq, struct mbuf *m_head)
{
    auto &txr = txq.cmd_ring;
    auto txd = txr.get_desc(txr.head);
    auto sop = txr.get_desc(txr.head);
    auto gen = txr.gen ^ 1; // Owned by cpu (yet)
    auto tx = 0;
    u64 tx_bytes = 0;
    int etype, proto, start;

    if (m_head->M_dat.MH.MH_pkthdr.csum_flags
        & (CSUM_TCP | CSUM_UDP | CSUM_TSO)) {
        int error = txq_offload(m_head, &etype, &proto, &start);
        if (error) {
            m_freem(m_head);
            return error;
        }
    }

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
        txd->layout->offload_mode = VMXNET3_OM_NONE;
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
        tx++;
    }
    txd->layout->eop = 1;
    txd->layout->compreq = 1;

    if (m_head->m_hdr.mh_flags & M_VLANTAG) {
        sop->layout->vtag_mode = 1;
        sop->layout->vtag = m_head->M_dat.MH.MH_pkthdr.ether_vtag;
    }

    if (m_head->M_dat.MH.MH_pkthdr.csum_flags & CSUM_TSO) {
        sop->layout->offload_mode = VMXNET3_OM_TSO;
        sop->layout->hlen = start;
        sop->layout->offload_pos = m_head->M_dat.MH.MH_pkthdr.tso_segsz;
    } else if (m_head->M_dat.MH.MH_pkthdr.csum_flags & (CSUM_TCP | CSUM_UDP)) {
        sop->layout->offload_mode = VMXNET3_OM_CSUM;
        sop->layout->hlen = start;
        sop->layout->offload_pos = start + m_head->M_dat.MH.MH_pkthdr.csum_data;
    }

    // Finally, change the ownership.
    wmb();
    sop->layout->gen ^= 1;

    if (++txq.layout->npending >= txq.layout->intr_threshold) {
        txq.layout->npending = 0;
        _bar0->writel(VMXNET3_BAR0_TXH, txr.head);
    }
    if (tx > 0) {
        if (txq.layout->npending > 0) {
            txq.layout->npending = 0;
            _bar0->writel(VMXNET3_BAR0_TXH, txr.head);
        }
    }

    _txq_stats.tx_bytes += tx_bytes;
    _txq_stats.tx_packets++;

    return 0;
}

int vmxnet3::txq_offload(struct mbuf *m, int *etype, int *proto, int *start)
{
    struct ether_vlan_header *evh;
    int offset;

    evh = mtod(m, struct ether_vlan_header *);
    if (evh->evl_encap_proto == htons(ETHERTYPE_VLAN)) {
        /* BMV: We should handle nested VLAN tags too. */
        *etype = ntohs(evh->evl_proto);
        offset = sizeof(struct ether_vlan_header);
    } else {
        *etype = ntohs(evh->evl_encap_proto);
        offset = sizeof(struct ether_header);
    }

    switch (*etype) {
    case ETHERTYPE_IP: {
        struct ip *ip, iphdr;
        if (__predict_false(m->m_hdr.mh_len < offset + static_cast<int>(sizeof(struct ip)))) {
            m_copydata(m, offset, sizeof(struct ip),
                (caddr_t) &iphdr);
            ip = &iphdr;
        } else
            ip = (struct ip *)(m->m_hdr.mh_data + offset);
        *proto = ip->ip_p;
        *start = offset + (ip->ip_hl << 2);
        break;
    }
    default:
        return (EINVAL);
    }

    if (m->M_dat.MH.MH_pkthdr.csum_flags & CSUM_TSO) {
        struct tcphdr *tcp, tcphdr;

        if (__predict_false(*proto != IPPROTO_TCP)) {
            /* Likely failed to correctly parse the mbuf. */
            return (EINVAL);
        }

        if (m->m_hdr.mh_len < *start + static_cast<int>(sizeof(struct tcphdr))) {
            m_copydata(m, offset, sizeof(struct tcphdr),
                (caddr_t) &tcphdr);
            tcp = &tcphdr;
        } else
            tcp = (struct tcphdr *)(m->m_hdr.mh_data + *start);

        /*
         * For TSO, the size of the protocol header is also
         * included in the descriptor header size.
         */
        *start += (tcp->th_off << 2);
    }

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

void vmxnet3::rxq_eof(vmxnet3_rxqueue &rxq)
{
    auto &rxc = rxq.comp_ring;

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
        auto &rxr = rxq.cmd_rings[rid];
        auto rxd = rxr.get_desc(idx);
        auto m = rxq.buf[rid][idx];

        assert(m != NULL);

        if (rxr.fill != idx) {
            while(rxr.fill != idx) {
                rxr.get_desc(rxr.fill)->layout->gen = rxr.gen;
                rxr.increment_fill();
            }
        }

        if (rxcd->layout->sop) {
            assert(rxd->layout->btype == VMXNET3_BTYPE_HEAD);
            assert((idx % 1) == 0);
            assert(rxq.m_currpkt_head == nullptr);

            if (length == 0) {
                rxq.discard(rid, idx);
                goto next;
            }

            rxq.newbuf(rid);

            m->M_dat.MH.MH_pkthdr.len = length;
            m->M_dat.MH.MH_pkthdr.rcvif = _ifn;
            m->M_dat.MH.MH_pkthdr.csum_flags = 0;
            m->m_hdr.mh_len = length;
            rxq.m_currpkt_head = rxq.m_currpkt_tail = m;
        } else {
            assert(rxd->layout->btype == VMXNET3_BTYPE_BODY);
            assert(rxq.m_currpkt_head != nullptr);

            rxq.newbuf(rid);

            m->m_hdr.mh_len = length;
            rxq.m_currpkt_head->M_dat.MH.MH_pkthdr.len += length;
            rxq.m_currpkt_tail->m_hdr.mh_next = m;
            rxq.m_currpkt_tail = m;
        }

        if (rxcd->layout->eop) {
            rxq_input(rxq, rxcd, rxq.m_currpkt_head);
            rxq.m_currpkt_head = rxq.m_currpkt_tail = nullptr;
        }

next:
        if (rxq.layout->update_rxhead) {
            idx = (idx + 1) % rxr.get_desc_num();
            if (rid == 0)
                _bar0->writel(VMXNET3_BAR0_RXH1, idx);
            else
                _bar0->writel(VMXNET3_BAR0_RXH2, idx);
        }
    }
}

bool vmxnet3::rxq_avail(vmxnet3_rxqueue &rxq)
{
    auto &rxc = rxq.comp_ring;
    auto rxcd = rxc.get_desc(rxc.next);
    assert(rxcd->layout->qid <= 2);

    return (rxcd->layout->gen == rxc.gen);
}

void vmxnet3::rx_csum(vmxnet3_rx_compdesc *rxcd, struct mbuf *m)
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

void vmxnet3::rxq_input(vmxnet3_rxqueue &rxq, vmxnet3_rx_compdesc *rxcd,
    struct mbuf *m)
{
    if (rxcd->layout->error) {
        m_freem(m);
        _rxq_stats.rx_csum_err++;
        return;
    }
    if (!rxcd->layout->no_csum)
        rx_csum(rxcd, m);
    _rxq_stats.rx_packets++;
    _rxq_stats.rx_bytes += m->M_dat.MH.MH_pkthdr.len;
    bool fast_path = _ifn->if_classifier.post_packet(m);
    if (!fast_path) {
        (*_ifn->if_input)(_ifn, m);
    }
}

void vmxnet3::get_mac_address(u_int8_t *macaddr)
{
    auto macl = read_cmd(VMXNET3_CMD_GET_MACL);
    auto mach = read_cmd(VMXNET3_CMD_GET_MACH);
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
