#ifndef VIRTIO_NET_DRIVER_H
#define VIRTIO_NET_DRIVER_H

#include "drivers/virtio.hh"
#include "drivers/pci-device.hh"

namespace virtio {

    class virtio_net : public virtio_driver {
    public:

        // The feature bitmap for virtio net
        enum NetFeatures {
            VIRTIO_NET_F_CSUM=0,            /* Host handles pkts w/ partial csum */
            VIRTIO_NET_F_GUEST_CSUM=1,      /* Guest handles pkts w/ partial csum */
            VIRTIO_NET_F_MAC=5,             /* Host has given MAC address. */
            VIRTIO_NET_F_GSO=6,             /* Host handles pkts w/ any GSO type */
            VIRTIO_NET_F_GUEST_TSO4=7,      /* Guest can handle TSOv4 in. */
            VIRTIO_NET_F_GUEST_TSO6=8,      /* Guest can handle TSOv6 in. */
            VIRTIO_NET_F_GUEST_ECN=9,       /* Guest can handle TSO[6] w/ ECN in. */
            VIRTIO_NET_F_GUEST_UFO=10,      /* Guest can handle UFO in. */
            VIRTIO_NET_F_HOST_TSO4=11,      /* Host can handle TSOv4 in. */
            VIRTIO_NET_F_HOST_TSO6=12,      /* Host can handle TSOv6 in. */
            VIRTIO_NET_F_HOST_ECN=13,       /* Host can handle TSO[6] w/ ECN in. */
            VIRTIO_NET_F_HOST_UFO=14,       /* Host can handle UFO in. */
            VIRTIO_NET_F_MRG_RXBUF=15,      /* Host can merge receive buffers. */
            VIRTIO_NET_F_STATUS=16,         /* virtio_net_config.status available */
            VIRTIO_NET_F_CTRL_VQ=17,        /* Control channel available */
            VIRTIO_NET_F_CTRL_RX=18,        /* Control channel RX mode support */
            VIRTIO_NET_F_CTRL_VLAN=19,      /* Control channel VLAN filtering */
            VIRTIO_NET_F_CTRL_RX_EXTRA=20,  /* Extra RX mode control support */
            VIRTIO_NET_F_GUEST_ANNOUNCE=21  /* Guest can announce device on the 
                                                                               network */
        };

        enum {
            VIRTIO_NET_DEVICE_ID=0x1000,
        };

        
        virtio_net();
        virtual ~virtio_net();

        virtual const std::string get_name(void) { return "virtio-net"; }

        virtual bool load(void);
        virtual bool unload(void);

        virtual u32 get_driver_features(void) { return ((1 << VIRTIO_NET_F_CSUM) | (1 << VIRTIO_NET_F_MAC)); }

    private:

        
    };

}

#endif

