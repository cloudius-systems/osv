
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
#include "debug.hh"

#include "sched.hh"

#include "drivers/clock.hh"
#include "drivers/clockevent.hh"

#include <osv/device.h>
#include <osv/ioctl.h>
#include <bsd/sys/net/ethernet.h>
#include <bsd/sys/net/if_types.h>

using namespace memory;

// TODO list
// irq thread affinity and tx affinity
// Mergable buffers
// tx zero copy
// vlans?

namespace virtio {

    int virtio_net::_instance = 0;

    #define virtio_net_tag "virtio-net"
    #define virtio_net_d(fmt)   logger::instance()->wrt(virtio_net_tag, logger_debug, (fmt))
    #define virtio_net_i(fmt)   logger::instance()->wrt(virtio_net_tag, logger_info, (fmt))
    #define virtio_net_w(fmt)   logger::instance()->wrt(virtio_net_tag, logger_warn, (fmt))
    #define virtio_net_e(fmt)   logger::instance()->wrt(virtio_net_tag, logger_error, (fmt))

    static int virtio_if_ioctl(
            struct ifnet *ifp,
            u_long command,
            caddr_t data)
    {
        virtio_net_d(fmt("virtio_if_ioctl %x") % command);

        int error = 0;
        switch(command) {
        case SIOCSIFMTU:
            virtio_net_d(fmt("SIOCSIFMTU"));
            break;
        case SIOCSIFFLAGS:
            virtio_net_d(fmt("SIOCSIFFLAGS"));
            /* Change status ifup, ifdown */
            if (ifp->if_flags & IFF_UP) {
                ifp->if_drv_flags |= IFF_DRV_RUNNING;
                virtio_net_d(fmt("if_up"));
            } else {
                ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
                virtio_net_d(fmt("if_down"));
            }
            break;
        case SIOCADDMULTI:
        case SIOCDELMULTI:
            virtio_net_d(fmt("SIOCDELMULTI"));
            break;
        default:
            virtio_net_d(fmt("redirecting to ether_ioctl()..."));
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

        virtio_net_d(fmt("%s_start (transmit)") % __FUNCTION__);

        /* Process packets */
        IF_DEQUEUE(&ifp->if_snd, m_head);
        while (m_head != NULL) {
            virtio_net_d(fmt("*** processing packet! ***"));

            vnet->tx(m_head, false);

            IF_DEQUEUE(&ifp->if_snd, m_head);
        }

        vnet->kick(1);
    }

    static void virtio_if_init(void* xsc)
    {
        virtio_net_d(fmt("Virtio-net init"));
    }

    virtio_net::virtio_net(pci::device& dev)
        : virtio_driver(dev)
    {
        _rx_queue = get_virt_queue(0);
        _tx_queue = get_virt_queue(1);

        std::stringstream ss;
        ss << "virtio-net";

        _driver_name = ss.str();
        virtio_i(fmt("VIRTIO NET INSTANCE"));
        _id = _instance++;

        read_config();

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
           virtio_net_w(fmt("if_alloc failed!"));
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
            virtio_net_i(fmt("The mac addr of the device is %x:%x:%x:%x:%x:%x") %
                    (u32)_config.mac[0] %
                    (u32)_config.mac[1] %
                    (u32)_config.mac[2] %
                    (u32)_config.mac[3] %
                    (u32)_config.mac[4] %
                    (u32)_config.mac[5]);

        return true;
    }

    struct virtio_net_req {
        struct virtio_net::virtio_net_hdr hdr;
        sglist payload;
        struct free_deleter {
            void operator()(struct mbuf *m) {m_freem(m);}
        };

        std::unique_ptr<struct mbuf, free_deleter> um;

        virtio_net_req() {memset(&hdr,0,sizeof(hdr));};
    };

    void virtio_net::receiver() {

        while (1) {

            // Wait for rx queue (used elements)
            virtio_driver::wait_for_queue(_rx_queue);

            int i = 0;
            u32 len;
            virtio_net_req * req;

            while((req = static_cast<virtio_net_req*>(_rx_queue->get_buf(&len))) != nullptr) {

                auto ii = req->payload._nodes.begin();
                ii++;

                auto m = req->um.release();
                u8* buf = mtod(m, u8*);
                virtio_net_d(fmt("\t got hdr len:%d = %d, len= %d vaddr=%p") % i++ % (int)req->hdr.hdr_len % len % (void*)buf);
                delete req;

                m->m_pkthdr.len = len;
                m->m_pkthdr.rcvif = _ifn;
                m->m_pkthdr.csum_flags = 0;
                m->m_len = len;

                _ifn->if_ipackets++;
                (*_ifn->if_input)(_ifn, m);
            }

            if (_rx_queue->avail_ring_has_room(_rx_queue->size()/2)) {
                virtio_net_d(fmt("ring is less than half full, refill"));
                fill_rx_ring();
            }

        }

    }

    static const int page_size = 4096;

    void virtio_net::fill_rx_ring()
    {
        virtio_net_d(fmt("%s") % __FUNCTION__);

        // it could have been a while (1) loop but it simplifies the allocation
        // tracking
        while (_rx_queue->avail_ring_has_room(2)) {
            virtio_net_req *req = new virtio_net_req;

            // As long as we're using standard MTU of 1500, it's fine to use
            // MCLBYTES
            struct mbuf *m = m_getjcl(M_NOWAIT, MT_DATA, M_PKTHDR, MCLBYTES);
            if (!m)
                break;
            req->um.reset(m);

            u8 *mdata;
            mdata = mtod(m, u8*);

            //Better use the header withing the mbuf allocation like in freebsd
            //offset = 0;
            //struct virtio_net_hdr *hdr = static_cast<struct virtio_net_hdr*>(mdata);
            //offset += sizeof(struct virtio_net_hdr);

            req->payload.add(mmu::virt_to_phys(mdata), MCLBYTES);
            req->payload.add(mmu::virt_to_phys(static_cast<void*>(&req->hdr)), sizeof(struct virtio_net_hdr), true);

            if (!_rx_queue->add_buf(&req->payload,0,req->payload.get_sgs(),req)) {
                delete req;
                break;
            }
        }

        _rx_queue->kick();
    }

    bool virtio_net::tx(struct mbuf *m_head, bool flush)
    {
        struct mbuf *m;
        virtio_net_req *req = new virtio_net_req;

        for (m = m_head; m != NULL; m = m->m_next) {
            if (m->m_len != 0) {
                virtio_net_d(fmt("Frag len=%d:") % m->m_len);
                req->payload.add(mmu::virt_to_phys(m->m_data), m->m_len);
            }
        }

        //TODO: verify what the hdr_len should be
        req->hdr.hdr_len = ETH_ALEN;
        req->um.reset(m_head);
        req->payload.add(mmu::virt_to_phys(static_cast<void*>(&req->hdr)), sizeof(struct virtio_net_hdr), true);
        // leak for now ; req->buffer = (u8*)out;

        if (!_tx_queue->avail_ring_has_room(req->payload.get_sgs())) {
            if (_tx_queue->used_ring_not_empty()) {
                virtio_net_d(fmt("%s: gc tx buffers to clear space"));
                tx_gc();
            } else {
                virtio_net_d(fmt("%s: no room") % __FUNCTION__);
                delete req;
                return false;
            }
        }

        if (!_tx_queue->add_buf(&req->payload, req->payload.get_sgs(),0,req)) {
            delete req;
            return false;
        }

        if (flush)
            _tx_queue->kick();

        return true;
    }

    void virtio_net::tx_gc_thread() {

        while (1) {
            // Wait for tx queue (used elements)
            virtio_driver::wait_for_queue(_tx_queue);
            tx_gc();
        }
    }

    void virtio_net::tx_gc()
    {
        int i = 0;
        u32 len;
        virtio_net_req * req;

        while((req = static_cast<virtio_net_req*>(_tx_queue->get_buf(&len))) != nullptr) {
            virtio_net_d(fmt("%s: gc %d") % __FUNCTION__ % i++);
            delete req;
        }
    }

    u32 virtio_net::get_driver_features(void)
    {
        u32 base = virtio_driver::get_driver_features();
        return (base | ( 1 << VIRTIO_NET_F_MAC));
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

