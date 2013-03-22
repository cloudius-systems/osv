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
#include <bsd/sys/sys/sockio.h>
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
        _msi.easy_register({ { 0, rx }, {1, tx }});

        //initialize the BSD interface _if
        _ifn = if_alloc(IFT_ETHER);
        if (_ifn == NULL) {
           //FIXME: need to handle this case - expand the above function not to allocate memory and
           // do it within the constructor.
           virtio_net_w(fmt("if_alloc failed!"));
           return;
        }

        if_initname(_ifn, _driver_name.c_str(), _id);
        _ifn->if_mtu = ETHERMTU;
        _ifn->if_softc = static_cast<void*>(this);
        _ifn->if_flags = IFF_BROADCAST /*| IFF_MULTICAST*/;
        _ifn->if_ioctl = virtio_if_ioctl;
        _ifn->if_start = virtio_if_start;
        _ifn->if_init = virtio_if_init;
        _ifn->if_snd.ifq_maxlen = _queues[1]->size();
        _ifn->if_capabilities = 0/* IFCAP_RXCSUM */;
        _ifn->if_capenable = _ifn->if_capabilities;

        ether_ifattach(_ifn, _config.mac);
        _msi.easy_register({ { 0, rx }, {1, tx }});

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
    struct ethhdr {
            unsigned char   h_dest[6];       /* destination eth addr */
            unsigned char   h_source[6];     /* source ether addr    */
            u16          h_proto;                /* packet type ID field */
    } __attribute__((packed));

    struct iphdr {
            u8    ihl:4,
                    version:4;
            u8    tos;
            u16  tot_len;
            u16  id;
            u16  frag_off;
            u8    ttl;
            u8    protocol;
            u16 check;
            u32  saddr;
            u32  daddr;
            /*The options start here. */
    } __attribute__((packed));


    struct icmphdr {
      u8          type;
      u8          code;
      u16       checksum;
      union {
            struct {
                    u16  id;
                    u16  sequence;
            } echo;
            u32  gateway;
            struct {
                    u16  __unused;
                    u16  mtu;
            } frag;
      } un;
    } __attribute__((packed));

    struct virtio_net_req {
        struct virtio_net::virtio_net_hdr hdr;
        sglist payload;
        struct mbuf *m;

        virtio_net_req() :m(nullptr) {memset(&hdr,0,sizeof(hdr));};
        ~virtio_net_req() {}
    };

    void virtio_net::receiver() {
        vring* queue = get_virt_queue(0);

        while (1) {
            sched::thread::wait_until([this, queue] {
                return queue->used_ring_not_empty();
            });

            int i = 0;
            u32 len;
            virtio_net_req * req;

            while((req = static_cast<virtio_net_req*>(queue->get_buf(&len))) != nullptr) {

                auto ii = req->payload._nodes.begin();
                ii++;

                struct mbuf *m = req->m;

                u8* buf = mtod(m, u8*);

                virtio_net_d(fmt("\t got hdr len:%d = %d, len= %d vaddr=%p") % i++ % (int)req->hdr.hdr_len % len % (void*)buf);

                m->m_pkthdr.len = len;
                m->m_pkthdr.rcvif = _ifn;
                m->m_pkthdr.csum_flags = 0;
                m->m_len = len;

                _ifn->if_ipackets++;
                (*_ifn->if_input)(_ifn, m);

                ethhdr* eh = reinterpret_cast<ethhdr*>(buf);
                virtio_net_d(fmt("The src %x:%x:%x:%x:%x:%x dst %x:%x:%x:%x:%x:%x type %d ") %
                        (u32)eh->h_source[0] %
                        (u32)eh->h_source[1] %
                        (u32)eh->h_source[2] %
                        (u32)eh->h_source[3] %
                        (u32)eh->h_source[4] %
                        (u32)eh->h_source[5] %
                        (u32)eh->h_dest[0] %
                        (u32)eh->h_dest[1] %
                        (u32)eh->h_dest[2] %
                        (u32)eh->h_dest[3] %
                        (u32)eh->h_dest[4] %
                        (u32)eh->h_dest[5] %
                        (u32)eh->h_proto);

                iphdr* ip = reinterpret_cast<iphdr*>(buf+sizeof(ethhdr));
                virtio_net_d(fmt("tot_len = %d protocol=%d, saddr=%d:%d:%d:%d daddr=%d:%d:%d:%d") %
                        (u32)ip->tot_len % (u32)ip->protocol % (ip->saddr & 0xff) % (ip->saddr >> 8 & 0xff) %
                        (ip->saddr >> 16 & 0xff) % (ip->saddr >> 24 & 0xff) % (ip->daddr & 0xff) % (ip->daddr >> 8 & 0xff) %
                        (ip->daddr >> 16 & 0xff) % (ip->daddr >> 24 & 0xff));

                icmphdr* icmp = reinterpret_cast<icmphdr*>(buf+sizeof(ethhdr)+sizeof(iphdr));
                virtio_net_d(fmt("icmp code=%d. type=%d") % (u32)icmp->code % (u32)icmp->type);

                delete req;
                // TODO: who should free it? m_freem(m);
            }

            if (queue->avail_ring_has_room(queue->size()/2)) {
                virtio_net_d(fmt("ring is less than half full, refill"));
                fill_rx_ring();
            }

        }

    }

    static const int page_size = 4096;

    void virtio_net::fill_rx_ring()
    {
        vring* queue = get_virt_queue(0);
        virtio_net_d(fmt("%s") % __FUNCTION__);

        // it could have been a while (1) loop but it simplifies the allocation
        // tracking
        while (queue->avail_ring_has_room(2)) {
            virtio_net_req *req = new virtio_net_req;

            struct mbuf *m = m_getjcl(M_NOWAIT, MT_DATA, M_PKTHDR, page_size);
            if (!m)
                break;

            req->m = m;
            u8 *mdata;
            //int offset;

            mdata = mtod(m, u8*);
            //Better use the header withing the mbuf allocation like in freebsd
            //offset = 0;
            //struct virtio_net_hdr *hdr = static_cast<struct virtio_net_hdr*>(mdata);
            //offset += sizeof(struct virtio_net_hdr);

            req->payload.add(mmu::virt_to_phys(mdata), page_size);
            req->payload.add(mmu::virt_to_phys(static_cast<void*>(&req->hdr)), sizeof(struct virtio_net_hdr), true);

            if (!queue->add_buf(&req->payload,0,req->payload.get_sgs(),req)) {
                delete req;
                m_freem(m);
                break;
            }
        }

        queue->kick();
    }

    bool virtio_net::tx(struct mbuf *m_head, bool flush)
    {
        vring* queue = get_virt_queue(1);
        struct mbuf *m;
        virtio_net_req *req = new virtio_net_req;

        for (m = m_head; m != NULL; m = m->m_next) {
            if (m->m_len != 0) {
                virtio_net_d(fmt("Frag len=%d:") % m->m_len);
                req->payload.add(mmu::virt_to_phys(m->m_data), m->m_len);
            }
        }

        req->hdr.hdr_len = ETH_ALEN + sizeof(iphdr);
        req->payload.add(mmu::virt_to_phys(static_cast<void*>(&req->hdr)), sizeof(struct virtio_net_hdr), true);
        // leak for now ; req->buffer = (u8*)out;

        if (!queue->avail_ring_has_room(req->payload.get_sgs())) {
            if (queue->used_ring_not_empty()) {
                virtio_net_d(fmt("%s: gc tx buffers to clear space"));
                tx_gc();
            } else {
                virtio_net_d(fmt("%s: no room") % __FUNCTION__);
                delete req;
                return false;
            }
        }

        if (!queue->add_buf(&req->payload, req->payload.get_sgs(),0,req)) {
            delete req;
            return false;
        }

        if (flush)
            queue->kick();

        return true;
    }

    void virtio_net::tx_gc_thread() {
        vring* queue = get_virt_queue(1);

        while (1) {
            sched::thread::wait_until([this, queue] {
                return queue->used_ring_not_empty();
            });

            tx_gc();
        }
    }

    void virtio_net::tx_gc()
    {
        int i = 0;
        u32 len;
        virtio_net_req * req;
        vring* queue = get_virt_queue(1);

        while((req = static_cast<virtio_net_req*>(queue->get_buf(&len))) != nullptr) {
            virtio_net_d(fmt("%s: gc %d") % __FUNCTION__ % i++);

            delete req;
            if (req->m) m_freem(req->m);
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

