/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef VMXNET3_QUEUES_H
#define VMXNET3_QUEUES_H

#include <bsd/porting/netport.h>

#include <atomic>
#include <functional>
#include <osv/mutex.h>
#include <osv/debug.hh>
#include <osv/mmu.hh>
#include <typeinfo>
#include <cxxabi.h>
#include <api/dlfcn.h>

namespace vmw {

enum {
    //Generic HW configuration
    VMXNET3_REVISION = 1,
    VMXNET3_UPT_VERSION = 1,
    VMXNET3_VERSIONS_MASK = 1,

    VMXNET3_REV1_MAGIC = 0XBABEFEE1,

    VMXNET3_GOS_FREEBSD = 0x10,
    VMXNET3_GOS_32BIT = 0x01,
    VMXNET3_GOS_64BIT = 0x02,

    //Mimic FreeBSD driver behavior
    VMXNET3_DRIVER_VERSION = 0x00010000,
    //TODO: Should we be more specific?
    VMXNET3_GUEST_OS_VERSION = 0x01,

    //Queues parameters
    VMXNET3_MAX_TX_QUEUES = 8,
    VMXNET3_MAX_RX_QUEUES = 16,
    VMXNET3_RXRINGS_PERQ = 2,

    VMXNET3_MAX_INTRS = VMXNET3_MAX_TX_QUEUES + VMXNET3_MAX_RX_QUEUES + 1,

    VMXNET3_MAX_TX_NDESC = 512,
    VMXNET3_MAX_RX_NDESC = 256,
    VMXNET3_MAX_TX_NCOMPDESC = VMXNET3_MAX_TX_NDESC,
    VMXNET3_MAX_RX_NCOMPDESC = VMXNET3_MAX_RX_NDESC * VMXNET3_RXRINGS_PERQ,

    VMXNET3_MAX_DESC_LEN = (1 << 14) - 1
};

template <class T> class vmxnet3_layout_holder {
public:
    void attach(void* storage) {
        layout = static_cast<T *>(storage);
        clear();
    }

    void clear() {
        memset(layout, 0, sizeof(*layout));
    }

    static size_t size() { return sizeof(*layout); }
    T *layout = nullptr;
};

struct vmxnet3_shared_layout {
    u32 magic;
    u32 pad1;

    // Misc. control
    u32 version;            // Driver version
    u32 guest;              // Guest OS
    u32 vmxnet3_revision;   // Supported VMXNET3 revision
    u32 upt_version;        // Supported UPT versions
    u64 upt_features;
    u64 driver_data;
    u64 queue_shared;
    u32 driver_data_len;
    u32 queue_shared_len;
    u32 mtu;
    u16 nrxsg_max;
    u8  ntxqueue;
    u8  nrxqueue;
    u32 reserved1[4];

    // Interrupt control
    u8  automask;
    u8  nintr;
    u8  evintr;
    u8  modlevel[VMXNET3_MAX_INTRS];
    u32 ictrl;
    u32 reserved2[2];

    // Receive filter parameters
    u32 rxmode;
    u16 mcast_tablelen;
    u16 pad2;
    u64 mcast_table;
    u32 vlan_filter[4096 / 32];

    struct {
        u32 version;
        u32 len;
        u64 paddr;
    }   rss, pm, plugin;

    u32 event;
    u32 reserved3[5];
} __packed;

class vmxnet3_drv_shared
    : public vmxnet3_layout_holder<vmxnet3_shared_layout> {
public:
    void attach(void* storage);
    void set_driver_data(mmu::phys pa, u32 len)
        { layout->driver_data = pa; layout->driver_data_len = len; }
    void set_queue_shared(mmu::phys pa, u32 len)
        {layout->queue_shared = pa; layout->queue_shared_len = len; }
    void set_max_sg_len(u16 num)
        { layout->nrxsg_max = num; }
    void set_mcast_table(mmu::phys pa, u32 len)
        { layout->mcast_table = pa; layout->mcast_tablelen = len; }
    void set_evt_intr_idx(u8 idx)
        { layout->evintr = idx; }
    void set_intr_config(u8 intr_num, u8 automask)
        { layout->nintr = intr_num; layout->automask = automask; }
};

struct vmxnet3_tx_desc_layout {
    u64    addr;

    u32    len:14;
    u32    gen:1;           // Generation
    u32    pad1:1;
    u32    dtype:1;         //Descriptor type
    u32    pad2:1;
    u32    offload_pos:14;  //Offloading position

    u32    hlen:10;         //Header len
    u32    offload_mode:2;  //Offloading mode
    u32    eop:1;           //End of packet
    u32    compreq:1;       //Completion request
    u32    pad3:1;
    u32    vtag_mode:1;     //VLAN tag insertion mode
    u32    vtag:16;         //VLAN tag
} __packed;


class vmxnet3_tx_desc
    : public vmxnet3_layout_holder<vmxnet3_tx_desc_layout> {
};

struct vmxnet3_tx_compdesc_layout {
    u32    eop_idx:12;      // EOP index in Tx ring
    u32    pad1:20;

    u32    pad2:32;
    u32    pad3:32;

    u32    rsvd:24;
    u32    type:7;
    u32    gen:1;
} __packed;

class vmxnet3_tx_compdesc
    : public vmxnet3_layout_holder<vmxnet3_tx_compdesc_layout> {
};

struct vmxnet3_rx_desc_layout {
    u64    addr;

    u32    len:14;
    u32    btype:1;         //Buffer type
    u32    dtype:1;         //Descriptor type
    u32    rsvd:15;
    u32    gen:1;

    u32    pad1:32;
} __packed;

class vmxnet3_rx_desc
    : public vmxnet3_layout_holder<vmxnet3_rx_desc_layout> {
};

struct vmxnet3_rx_compdesc_layout {
    u32     rxd_idx:12;     //Rx descriptor index
    u32     pad1:2;
    u32     eop:1;          //End of packet
    u32     sop:1;          //Start of packet
    u32     qid:10;
    u32     rss_type:4;
    u32     no_csum:1;      //No checksum calculated
    u32     pad2:1;

    u32     rss_hash:32;    //RSS hash value

    u32     len:14;
    u32     error:1;
    u32     vlan:1;         //802.1Q VLAN frame
    u32     vtag:16;        //VLAN tag

    u32     csum:16;
    u32     csum_ok:1;      //TCP/UDP checksum ok
    u32     udp:1;
    u32     tcp:1;
    u32     ipcsum_ok:1;    //IP checksum OK
    u32     ipv6:1;
    u32     ipv4:1;
    u32     fragment:1;     //IP fragment
    u32     fcs:1;          //Frame CRC correct
    u32     type:7;
    u32     gen:1;
} __packed;

class vmxnet3_rx_compdesc
    : public vmxnet3_layout_holder<vmxnet3_rx_compdesc_layout> {
};

struct UPT1_TxStats {
    u64    TSO_packets;
    u64    TSO_bytes;
    u64    ucast_packets;
    u64    ucast_bytes;
    u64    mcast_packets;
    u64    mcast_bytes;
    u64    bcast_packets;
    u64    bcast_bytes;
    u64    error;
    u64    discard;
} __packed;

struct vmxnet3_txq_shared_layout {
    // Control
    u32    npending;
    u32    intr_threshold;
    u64    reserved1;

    // Config
    u64    cmd_ring;
    u64    data_ring;
    u64    comp_ring;
    u64    driver_data;
    u64    reserved2;
    u32    cmd_ring_len;
    u32    data_ring_len;
    u32    comp_ring_len;
    u32    driver_data_len;
    u8     intr_idx;
    u8     pad1[7];

    // Queue status
    u8     stopped;
    u8     pad2[3];
    u32    error;

    struct UPT1_TxStats stats;

    u8      pad3[88];
} __packed;

class vmxnet3_txq_shared
    : public vmxnet3_layout_holder<vmxnet3_txq_shared_layout> {
};

struct UPT1_RxStats {
    u64   LRO_packets;
    u64   LRO_bytes;
    u64   ucast_packets;
    u64   ucast_bytes;
    u64   mcast_packets;
    u64   mcast_bytes;
    u64   bcast_packets;
    u64   bcast_bytes;
    u64   nobuffer;
    u64   error;
} __packed;

struct vmxnet3_rxq_shared_layout {
    u8      update_rxhead;
    u8      pad1[7];
    u64     reserved1;

    u64     cmd_ring[VMXNET3_RXRINGS_PERQ];
    u64     comp_ring;
    u64     driver_data;
    u64     reserved2;
    u32     cmd_ring_len[VMXNET3_RXRINGS_PERQ];
    u32     comp_ring_len;
    u32     driver_data_len;
    u8      intr_idx;
    u8      pad2[7];

    u8      stopped;
    u8      pad3[3];
    u32     error;

    struct  UPT1_RxStats stats;

    u8      pad4[88];
} __packed;

class vmxnet3_rxq_shared
    : public vmxnet3_layout_holder<vmxnet3_rxq_shared_layout> {
};

}

#endif // VIRTIO_VRING_H
