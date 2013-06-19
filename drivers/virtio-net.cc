
#define _KERNEL

#include <sys/cdefs.h>

#include "drivers/virtio.hh"
#include "drivers/virtio-net.hh"
#include "drivers/pci-device.hh"
#include "interrupt.hh"

#include "mempool.hh"
#include "mmu.hh"
#include "sglist.hh"

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

TRACEPOINT(trace_virtio_net_rx_packet, "if=%d, len=%d", int, int);
TRACEPOINT(trace_virtio_net_rx_wake, "");
TRACEPOINT(trace_virtio_net_tx_packet, "if=%d, len=%d", int, int);
TRACEPOINT(trace_virtio_net_tx_wake, "");

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
    // Main transmit routine.
    static void virtio_if_start(struct ifnet* ifp)
    {
        struct mbuf* m_head = NULL;
        virtio_net* vnet = (virtio_net*)ifp->if_softc;

        virtio_net_d("%s_start (transmit)", __FUNCTION__);

        /* Process packets */
        IF_DEQUEUE(&ifp->if_snd, m_head);
        while (m_head != NULL) {
            virtio_net_d("*** processing packet! ***");

            vnet->tx(m_head, false);

            IF_DEQUEUE(&ifp->if_snd, m_head);
        }

        vnet->kick(1);
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

        //register the 3 irq callback for the net
        sched::thread* rx = new sched::thread([this] { this->receiver(); });
        sched::thread* tx = new sched::thread([this] { this->tx_gc_thread(); });
        rx->start();
        tx->start();

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
        _ifn->if_start = virtio_if_start;
        _ifn->if_init = virtio_if_init;
        _ifn->if_snd.ifq_maxlen = _tx_queue->size();
        _ifn->if_capabilities = 0/* IFCAP_RXCSUM */;
        _ifn->if_capenable = _ifn->if_capabilities;

        ether_ifattach(_ifn, _config.mac);
        _msi.easy_register({
            { 0, [&] { _rx_queue->disable_interrupts(); }, rx },
            { 1, [&] { _tx_queue->disable_interrupts(); }, tx }
        });

        fill_rx_ring();

        add_dev_status(VIRTIO_CONFIG_S_DRIVER_OK);
    }

    virtio_net::~virtio_net()
    {
        //TODO: In theory maintain the list of free instances and gc it
        // including the thread objects and their stack

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

        return true;
    }

    struct virtio_net_req {
        struct virtio_net::virtio_net_hdr_mrg_rxbuf mhdr;
        sglist payload;
        struct free_deleter {
            void operator()(struct mbuf *m) {m_freem(m);}
        };

        std::unique_ptr<struct mbuf, free_deleter> um;

        virtio_net_req() {memset(&mhdr,0,sizeof(mhdr));};
    };

    void virtio_net::receiver() {

        while (1) {

            // Wait for rx queue (used elements)
            virtio_driver::wait_for_queue(_rx_queue);
            trace_virtio_net_rx_wake();

            u32 len;
            struct mbuf* m;
            int nbufs;
            u32 offset = _hdr_size;
            //use local header that we copy out of the mbuf since we're truncating it
            struct virtio_net_hdr_mrg_rxbuf mhdr;

            while ((m = static_cast<struct mbuf*>(_rx_queue->get_buf(&len))) != nullptr) {

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
                    if (nbufs > 1) // It should happen but it doesn't, leave the comment for visibility in the mean time
                        virtio_net_e("\t RX: mergeable, hdr len: %d, len %d, num_bufs=%d", mhdr.hdr.hdr_len, len, mhdr.num_buffers);
                }

                m->m_pkthdr.len = len;
                m->m_pkthdr.rcvif = _ifn;
                m->m_pkthdr.csum_flags = 0;
                m->m_len = len;

                struct mbuf* m_head, *m_tail;
                m_tail = m_head = m;

                while (--nbufs > 0) {
                    if ((m = static_cast<struct mbuf*>(_rx_queue->get_buf(&len))) == nullptr) {
                        _ifn->if_ierrors++;
                        break;
                    }

                    if (m->m_len < (int)len)
                        len = m->m_len;

                    m->m_len = len;
                    m->m_flags &= ~M_PKTHDR;
                    m_head->m_pkthdr.len += len;
                    m_tail->m_next = m;
                    m_tail = m;
                }

                // skip over the virtio header bytes (offset) that aren't need for the above layer
                m_adj(m_head, offset);

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

    static const int page_size = 4096;

    void virtio_net::fill_rx_ring()
    {
        bool worked = false;

        while (_rx_queue->avail_ring_not_empty()) {
            sglist payload;
            struct mbuf *m = m_getjcl(M_NOWAIT, MT_DATA, M_PKTHDR, MCLBYTES);
            if (!m)
                break;

            m->m_len = MCLBYTES;
            u8 *mdata = mtod(m, u8*);

            payload.add(mmu::virt_to_phys(mdata), m->m_len);
            if (!_rx_queue->add_buf(&payload,0,payload.get_sgs(),m)) {
                m_freem(m);
                break;
            }
            worked = true;
        }

        if (worked) _rx_queue->kick();
    }


    bool virtio_net::tx(struct mbuf *m_head, bool flush)
    {
        struct mbuf *m;
        virtio_net_req *req = new virtio_net_req;

        req->um.reset(m_head);
        req->payload.add(mmu::virt_to_phys(static_cast<void*>(&req->mhdr)), _hdr_size);

        for (m = m_head; m != NULL; m = m->m_next) {
            if (m->m_len != 0) {
                virtio_net_d("Frag len=%d:", m->m_len);
                req->mhdr.num_buffers++;
                req->payload.add(mmu::virt_to_phys(m->m_data), m->m_len);
            }
        }

        if (!_tx_queue->avail_ring_has_room(req->payload.get_sgs())) {
            if (_tx_queue->used_ring_not_empty()) {
                virtio_net_d("%s: gc tx buffers to clear space");
                tx_gc();
            } else {
                virtio_net_d("%s: no room", __FUNCTION__);
                delete req;
                return false;
            }
        }

        if (!_tx_queue->add_buf(&req->payload, req->payload.get_sgs(),0,req)) {
            delete req;
            return false;
        }

        trace_virtio_net_tx_packet(_ifn->if_index, req->payload.len());

        if (flush)
            _tx_queue->kick();

        return true;
    }

    void virtio_net::tx_gc_thread() {

        while (1) {
            // Wait for tx queue (used elements)
            virtio_driver::wait_for_queue(_tx_queue);
            trace_virtio_net_tx_wake();
            tx_gc();
        }
    }

    void virtio_net::tx_gc()
    {
        u32 len;
        virtio_net_req * req;

        while((req = static_cast<virtio_net_req*>(_tx_queue->get_buf(&len))) != nullptr) {
            delete req;
        }
    }

    u32 virtio_net::get_driver_features(void)
    {
        u32 base = virtio_driver::get_driver_features();
        return (base | ( 1 << VIRTIO_NET_F_MAC) | (1 << VIRTIO_NET_F_MRG_RXBUF));
    }

    hw_driver* virtio_net::probe(hw_device* dev)
    {
        if (auto pci_dev = dynamic_cast<pci::device*>(dev)) {
            if (pci_dev->get_id() == hw_device_id(VIRTIO_VENDOR_ID, VIRTIO_NET_DEVICE_ID)) {
                return new virtio_net(*pci_dev);
            }
        }
        return nullptr;
    }

}

