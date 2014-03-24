/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef VMWARE3_DRIVER_H
#define VMWARE3_DRIVER_H

#include <bsd/porting/netport.h>
#include <bsd/sys/net/if_var.h>
#include <bsd/sys/net/if.h>
#include <bsd/sys/sys/mbuf.h>

#include "drivers/driver.hh"
#include "drivers/vmxnet3-queues.hh"
#include "drivers/pci-device.hh"
#include <osv/mempool.hh>
#include <osv/interrupt.hh>

namespace vmw {

template<class DescT, int NDesc>
    class vmxnet3_ring {
    public:
        vmxnet3_ring()
            : _desc_mem(DescT::size() * NDesc, VMXNET3_DESC_ALIGN)
        {
            void *va = _desc_mem.get_va();
            slice_memory(va, _desc);
        }

        unsigned head, next, fill;
        int gen;
        mmu::phys get_desc_pa() const { return _desc_mem.get_pa(); }
        static u32 get_desc_num() { return NDesc; }
        DescT* get_desc(int i) { return &_desc[i]; }
        void clear_desc(int i) { _desc[i].clear(); }
        void clear_descs() { for (int i = 0; i < NDesc; i++) clear_desc(i); }
        void increment_fill();
    private:
        enum {
            //Queue descriptors alignment
            VMXNET3_DESC_ALIGN = 512
        };
        memory::phys_contiguous_memory _desc_mem;
        DescT      _desc[NDesc];
    };

class vmxnet3_txqueue : public vmxnet3_txq_shared {
public:
    void init();
    void set_intr_idx(unsigned idx) { layout->intr_idx = static_cast<u8>(idx); }
    int enqueue(struct mbuf *m);
    typedef vmxnet3_ring<vmxnet3_tx_desc, VMXNET3_MAX_TX_NDESC> cmdRingT;
    typedef vmxnet3_ring<vmxnet3_tx_compdesc, VMXNET3_MAX_TX_NCOMPDESC> compRingT;
    cmdRingT cmd_ring;

    compRingT comp_ring;
    struct mbuf *buf[VMXNET3_MAX_TX_NDESC];
    int avail = VMXNET3_MAX_TX_NDESC;
};

class vmxnet3_rxqueue : public vmxnet3_rxq_shared {
public:
    void init();
    void set_intr_idx(unsigned idx) { layout->intr_idx = static_cast<u8>(idx); }
    void discard(int rid, int idx);
    void discard_chain(int rid);
    int newbuf(int rid);

    typedef vmxnet3_ring<vmxnet3_rx_desc, VMXNET3_MAX_RX_NDESC> cmdRingT;
    typedef vmxnet3_ring<vmxnet3_rx_compdesc, VMXNET3_MAX_RX_NCOMPDESC> compRingT;

    cmdRingT cmd_rings[VMXNET3_RXRINGS_PERQ];
    compRingT comp_ring;
    struct mbuf *buf[VMXNET3_RXRINGS_PERQ][VMXNET3_MAX_RX_NDESC];
};

class vmxnet3 : public hw_driver {
public:
    enum {
        VMXNET3_INIT_GEN = 1,

        // Buffer types
        VMXNET3_BTYPE_HEAD = 0, // Head only
        VMXNET3_BTYPE_BODY = 1 // Body only
    };
    explicit vmxnet3(pci::device& dev);
    virtual ~vmxnet3() {};

    virtual const std::string get_name() { return std::string("vmxnet3"); }

    virtual void dump_config(void);
    int transmit(struct mbuf* m_head);
    void receive_work();

    static hw_driver* probe(hw_device* dev);

    /**
     * Fill the if_data buffer with data from our iface including those that
     * we have gathered by ourselvs (e.g. FP queue stats).
     * @param out_data output buffer
     */
    void fill_stats(struct if_data* out_data) const;

private:
    enum {
        VMXNET3_VENDOR_ID = 0x15AD,
        VMXNET3_DEVICE_ID = 0x07B0,

        //Queues number
        VMXNET3_TX_QUEUES = 1,
        VMXNET3_RX_QUEUES = 1,

        //BAR0 registers
        VMXNET3_BAR0_TXH = 0x600, // Queue 0 of Tx head
        VMXNET3_BAR0_RXH1 = 0x800, // Queue 0 of Ring1 Rx head
        VMXNET3_BAR0_RXH2 = 0xA00, // Queue 0 of Ring2 Rx head

        //BAR1 registers
        VMXNET3_BAR1_VRRS = 0x000,    // Revision
        VMXNET3_BAR1_UVRS = 0x008,    // UPT version
        VMXNET3_BAR1_DSL  = 0x010,    // Driver shared address low
        VMXNET3_BAR1_DSH  = 0x018,    // Driver shared address high
        VMXNET3_BAR1_CMD  = 0x020,    // Command

        //VMXNET3 commands
        VMXNET3_CMD_ENABLE = 0xCAFE0000, // Enable VMXNET3
        VMXNET3_CMD_DISABLE = 0xCAFE0001, // Disable VMXNET3
        VMXNET3_CMD_RESET = 0xCAFE0002, // Reset device
        VMXNET3_CMD_SET_RXMODE = 0xCAFE0003, // Set interface flags
        VMXNET3_CMD_SET_FILTER = 0xCAFE0004, // Set address filter
        VMXNET3_CMD_VLAN_FILTER = 0xCAFE0005, // Set VLAN filter
        VMXNET3_CMD_GET_STATUS = 0xF00D0000,  // Get queue errors
        VMXNET3_CMD_GET_STATS = 0xF00D0001, // Get queue statistics
        VMXNET3_CMD_GET_LINK = 0xF00D0002, // Get link status
        VMXNET3_CMD_GET_MACL = 0xF00D0003, // Get MAC address low
        VMXNET3_CMD_GET_MACH = 0xF00D0004, // Get MAC address high
        VMXNET3_CMD_GET_INTRCFG = 0xF00D0008,  // Get interrupt config

        //Shared memory alignment
        VMXNET3_DRIVER_SHARED_ALIGN = 1,
        VMXNET3_QUEUES_SHARED_ALIGN = 128,
        VMXNET3_MULTICAST_ALIGN = 32,

        //Generic definitions
        VMXNET3_ETH_ALEN = 6,

        //Internal device parameters
        VMXNET3_MULTICAST_MAX = 32,
        VMXNET3_MAX_RX_SEGS = 17,
        VMXNET3_NUM_INTRS = 3,

        //Offloading modes
        VMXNET3_OM_NONE = 0,
        VMXNET3_OM_CSUM = 2,
        VMXNET3_OM_TSO = 3,

        //RX modes
        VMXNET3_RXMODE_UCAST = 0x01,
        VMXNET3_RXMODE_MCAST = 0x02,
        VMXNET3_RXMODE_BCAST = 0x04,
        VMXNET3_RXMODE_ALLMULTI = 0x08,
        VMXNET3_RXMODE_PROMISC = 0x10,

        //Hardware features
        UPT1_F_CSUM = 0x0001,
        UPT1_F_RSS = 0x0002,
        UPT1_F_VLAN = 0x0004,
        UPT1_F_LRO = 0x0008
    };
    static inline constexpr u32 VMXNET3_BAR0_IMASK(int irq)
    {
        return 0x000 + irq * 8;
    }

    void parse_pci_config();
    void stop();
    void enable_device();
    void do_version_handshake();
    void attach_queues_shared();
    void fill_driver_shared();
    void allocate_interrupts();

    void set_intr_idx(unsigned idx) { _drv_shared.set_evt_intr_idx(static_cast<u8>(idx)); }
    virtual void isr() {};

    void write_cmd(u32 cmd);
    u32 read_cmd(u32 cmd);

    void get_mac_address(u_int8_t *macaddr);
    int txq_encap(vmxnet3_txqueue &txq, struct mbuf *m_head);
    int txq_offload(struct mbuf *m, int *etype, int *proto, int *start);
    void txq_gc(vmxnet3_txqueue &txq);
    void rxq_eof(vmxnet3_rxqueue &rxq);
    bool rxq_avail(vmxnet3_rxqueue &rxq);
    void rxq_input(vmxnet3_rxqueue &rxq, vmxnet3_rx_compdesc *rxcd,
        struct mbuf *m);
    void rx_csum(vmxnet3_rx_compdesc *rxcd, struct mbuf *m);
    void enable_interrupts();
    void enable_interrupt(unsigned idx);
    void disable_interrupts();
    void disable_interrupt(unsigned idx);

    //maintains the vmxnet3 instance number for multiple adapters
    static int _instance;
    int _id;
    struct ifnet* _ifn;

    pci::device& _dev;
    interrupt_manager _msi;

    struct rxq_stats {
        u64 rx_packets; /* if_ipackets */
        u64 rx_bytes;   /* if_ibytes */
        u64 rx_drops;   /* if_iqdrops */
        u64 rx_csum;    /* number of packets with correct csum */
        u64 rx_csum_err;/* number of packets with a bad checksum */
    } _rxq_stats = { 0 };

    struct txq_stats {
        u64 tx_packets; /* if_opackets */
        u64 tx_bytes;   /* if_obytes */
        u64 tx_err;     /* Number of broken packets */
        u64 tx_drops;   /* Number of dropped packets */
        u64 tx_csum;    /* CSUM offload requests */
        u64 tx_tso;     /* GSO/TSO packets */
        /* u64 tx_rescheduled; */ /* TODO when we implement xoff */
    } _txq_stats = { 0 };

    //Shared memory
    pci::bar *_bar0 = nullptr;
    pci::bar *_bar1 = nullptr;

    memory::phys_contiguous_memory _drv_shared_mem;
    vmxnet3_drv_shared _drv_shared;

    memory::phys_contiguous_memory _queues_shared_mem;

    vmxnet3_txqueue _txq[VMXNET3_TX_QUEUES];
    vmxnet3_rxqueue _rxq[VMXNET3_RX_QUEUES];

    memory::phys_contiguous_memory _mcast_list;

    mutex _txq_lock;

    sched::thread _receive_task;
};

}

#endif
