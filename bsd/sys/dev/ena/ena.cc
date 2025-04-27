/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2015-2021 Amazon.com, Inc. or its affiliates.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/eventhandler.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/smp.h>
#include <bsd/sys/sys/socket.h>
#include <sys/sysctl.h>
#include <sys/taskqueue.h>
#include <sys/time.h>
#include <sys/ioctl.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/atomic.h>
#include <machine/bus.h>
#include <machine/in_cksum.h>
#include <machine/resource.h>

#include <bsd/sys/net/bpf.h>
#include <bsd/sys/net/ethernet.h>
#include <bsd/sys/net/if.h>
#include <bsd/sys/net/if_arp.h>
#include <bsd/sys/net/if_dl.h>
#include <bsd/sys/net/if_media.h>
#include <bsd/sys/net/if_types.h>
#include <bsd/sys/net/if_var.h>
#include <bsd/sys/net/if_vlan_var.h>
#include <bsd/sys/netinet/in.h>
#include <bsd/sys/netinet/in_systm.h>
#include <bsd/sys/netinet/if_ether.h>
#include <bsd/sys/netinet/ip.h>
//#include <bsd/sys/netinet/ip6.h>
#include <bsd/sys/netinet/tcp.h>
#include <bsd/sys/netinet/udp.h>

//#define ENA_LOG_ENABLE 1
//#define ENA_LOG_IO_ENABLE 1

#include "ena.h"
#include "ena_datapath.h"

#include <osv/mmu.hh>
#include <osv/mempool.hh>
#include <osv/aligned_new.hh>
#include <osv/msi.hh>
#include <osv/sched.hh>
#include <osv/trace.hh>

int ena_log_level = ENA_INFO;

static inline void critical_enter()  { sched::preempt_disable(); }
static inline void critical_exit() { sched::preempt_enable(); }

#include <sys/buf_ring.h>

extern "C" {
struct buf_ring *buf_ring_alloc(int count, int type, int flags,
    struct mtx *);
void buf_ring_free(struct buf_ring *br, int type);
}


/*********************************************************
 *  Function prototypes
 *********************************************************/
static void ena_intr_msix_mgmnt(void *);
static void ena_free_pci_resources(struct ena_adapter *);
static int ena_change_mtu(if_t, int);
static inline void ena_alloc_counters(counter_u64_t *, int);
static inline void ena_free_counters(counter_u64_t *, int);
static inline void ena_reset_counters(counter_u64_t *, int);
static void ena_init_io_rings_common(struct ena_adapter *, struct ena_ring *,
    uint16_t);
static void ena_init_io_rings_basic(struct ena_adapter *);
static void ena_init_io_rings_advanced(struct ena_adapter *);
static void ena_init_io_rings(struct ena_adapter *);
static void ena_free_io_ring_resources(struct ena_adapter *, unsigned int);
static void ena_free_all_io_rings_resources(struct ena_adapter *);
static int ena_setup_tx_resources(struct ena_adapter *, int);
static void ena_free_tx_resources(struct ena_adapter *, int);
static int ena_setup_all_tx_resources(struct ena_adapter *);
static void ena_free_all_tx_resources(struct ena_adapter *);
static int ena_setup_rx_resources(struct ena_adapter *, unsigned int);
static void ena_free_rx_resources(struct ena_adapter *, unsigned int);
static int ena_setup_all_rx_resources(struct ena_adapter *);
static void ena_free_all_rx_resources(struct ena_adapter *);
static inline int ena_alloc_rx_mbuf(struct ena_adapter *, struct ena_ring *,
    struct ena_rx_buffer *);
static void ena_free_rx_mbuf(struct ena_adapter *, struct ena_ring *,
    struct ena_rx_buffer *);
static void ena_free_rx_bufs(struct ena_adapter *, unsigned int);
static void ena_refill_all_rx_bufs(struct ena_adapter *);
static void ena_free_all_rx_bufs(struct ena_adapter *);
static void ena_free_tx_bufs(struct ena_adapter *, unsigned int);
static void ena_free_all_tx_bufs(struct ena_adapter *);
static void ena_destroy_all_tx_queues(struct ena_adapter *);
static void ena_destroy_all_rx_queues(struct ena_adapter *);
static void ena_destroy_all_io_queues(struct ena_adapter *);
static int ena_create_io_queues(struct ena_adapter *);
static int ena_handle_msix(void *);
static int ena_enable_msix(struct ena_adapter *);
static void ena_setup_mgmnt_intr(struct ena_adapter *);
static int ena_setup_io_intr(struct ena_adapter *);
static int ena_request_mgmnt_irq(struct ena_adapter *);
static int ena_request_io_irq(struct ena_adapter *);
static void ena_free_mgmnt_irq(struct ena_adapter *);
static void ena_free_io_irq(struct ena_adapter *);
static void ena_free_irqs(struct ena_adapter *);
static void ena_disable_msix(struct ena_adapter *);
static void ena_unmask_all_io_irqs(struct ena_adapter *);
static int ena_up_complete(struct ena_adapter *);
static void ena_init(void *);
static int ena_ioctl(if_t, u_long, caddr_t);
static int ena_get_dev_offloads(struct ena_com_dev_get_features_ctx *);
static void ena_update_hwassist(struct ena_adapter *);
static int ena_setup_ifnet(pci::device *, struct ena_adapter *,
    struct ena_com_dev_get_features_ctx *);
static int ena_set_queues_placement_policy(pci::device *, struct ena_com_dev *,
    struct ena_admin_feature_llq_desc *, struct ena_llq_configurations *);
static uint32_t ena_calc_max_io_queue_num(pci::device *, struct ena_com_dev *,
    struct ena_com_dev_get_features_ctx *);
static int ena_calc_io_queue_size(struct ena_calc_queue_size_ctx *);
static void ena_config_host_info(struct ena_com_dev *, pci::device*);
static int ena_device_init(struct ena_adapter *, pci::device *,
    struct ena_com_dev_get_features_ctx *, int *);
static int ena_enable_msix_and_set_admin_interrupts(struct ena_adapter *);
static void ena_update_on_link_change(void *, struct ena_admin_aenq_entry *);
static void unimplemented_aenq_handler(void *, struct ena_admin_aenq_entry *);
static void ena_timer_service(void *);

#ifdef ENA_LOG_ENABLE
static char ena_version[] = ENA_DEVICE_NAME ENA_DRV_MODULE_NAME
    " v" ENA_DRV_MODULE_VERSION;
#endif

static ena_vendor_info_t ena_vendor_info_array[] = {
	{ PCI_VENDOR_ID_AMAZON, PCI_DEV_ID_ENA_PF, 0 },
	{ PCI_VENDOR_ID_AMAZON, PCI_DEV_ID_ENA_PF_RSERV0, 0 },
	{ PCI_VENDOR_ID_AMAZON, PCI_DEV_ID_ENA_VF, 0 },
	{ PCI_VENDOR_ID_AMAZON, PCI_DEV_ID_ENA_VF_RSERV0, 0 },
	/* Last entry */
	{ 0, 0, 0 }
};

struct sx ena_global_lock;

int
ena_dma_alloc(device_t dmadev, bus_size_t size, ena_mem_handle_t *dma,
    int mapflags, bus_size_t alignment, int domain)
{
	dma->vaddr = (caddr_t)memory::alloc_phys_contiguous_aligned(size, mmu::page_size);
	if (!dma->vaddr) {
		ena_log(pdev, ERR, "memory::alloc_phys_contiguous_aligned failed!", 1);
		dma->vaddr = 0;
		dma->paddr = 0;
		return ENA_COM_NO_MEM;
	}

	dma->paddr = mmu::virt_to_phys(dma->vaddr);

	return (0);
}

static void
ena_free_pci_resources(struct ena_adapter *adapter)
{
	if (adapter->registers != NULL) {
		adapter->registers->unmap();
	}
}

bool
ena_probe(pci::device* pdev)
{
	ena_vendor_info_t *ent = ena_vendor_info_array;
	while (ent->vendor_id != 0) {
		if (pdev->get_id() == hw_device_id(ent->vendor_id, ent->device_id)) {
			ena_log_raw(DBG, "vendor=%x device=%x", ent->vendor_id,
			    ent->device_id);

			return true;
		}

		ent++;
	}

	return false;
}

static int
ena_change_mtu(if_t ifp, int new_mtu)
{
	struct ena_adapter *adapter = (ena_adapter*)ifp->if_softc;
	int rc;

	if ((new_mtu > adapter->max_mtu) || (new_mtu < ENA_MIN_MTU)) {
		ena_log(pdev, ERR, "Invalid MTU setting. new_mtu: %d max mtu: %d min mtu: %d",
		    new_mtu, adapter->max_mtu, ENA_MIN_MTU);
		return (EINVAL);
	}

	rc = ena_com_set_dev_mtu(adapter->ena_dev, new_mtu);
	if (likely(rc == 0)) {
		ena_log(pdev, DBG, "set MTU to %d", new_mtu);
		ifp->if_mtu = new_mtu;
	} else {
		ena_log(pdev, ERR, "Failed to set MTU to %d", new_mtu);
	}

	return (rc);
}

//Later todo - Disable counters for now
static inline void
ena_alloc_counters(counter_u64_t *begin, int size)
{
/*	counter_u64_t *end = (counter_u64_t *)((char *)begin + size);

	for (; begin < end; ++begin)
		*begin = counter_u64_alloc(M_WAITOK);*/
}

static inline void
ena_free_counters(counter_u64_t *begin, int size)
{
/*	counter_u64_t *end = (counter_u64_t *)((char *)begin + size);

	for (; begin < end; ++begin)
		counter_u64_free(*begin);*/
}

static inline void
ena_reset_counters(counter_u64_t *begin, int size)
{
/*	counter_u64_t *end = (counter_u64_t *)((char *)begin + size);

	for (; begin < end; ++begin)
		counter_u64_zero(*begin);*/
}

static void
ena_init_io_rings_common(struct ena_adapter *adapter, struct ena_ring *ring,
    uint16_t qid)
{
	ring->qid = qid;
	ring->adapter = adapter;
	ring->ena_dev = adapter->ena_dev;
	ring->first_interrupt.store(0);
	ring->no_interrupt_event_cnt = 0;
}

static void
ena_init_io_rings_basic(struct ena_adapter *adapter)
{
	struct ena_com_dev *ena_dev;
	struct ena_ring *txr, *rxr;
	struct ena_que *que;
	int i;

	ena_dev = adapter->ena_dev;

	for (i = 0; i < adapter->num_io_queues; i++) {
		txr = &adapter->tx_ring[i];
		rxr = &adapter->rx_ring[i];

		/* TX/RX common ring state */
		ena_init_io_rings_common(adapter, txr, i);
		ena_init_io_rings_common(adapter, rxr, i);

		/* TX specific ring state */
		txr->tx_max_header_size = ena_dev->tx_max_header_size;
		txr->tx_mem_queue_type = ena_dev->tx_mem_queue_type;

		que = &adapter->que[i];
		que->adapter = adapter;
		que->id = i;
		que->tx_ring = txr;
		que->rx_ring = rxr;

		txr->que = que;
		rxr->que = que;

		rxr->empty_rx_queue = 0;
		rxr->rx_mbuf_sz = MJUMPAGESIZE;
	}
}

static void
ena_init_io_rings_advanced(struct ena_adapter *adapter)
{
	struct ena_ring *txr, *rxr;
	int i;

	for (i = 0; i < adapter->num_io_queues; i++) {
		txr = &adapter->tx_ring[i];
		rxr = &adapter->rx_ring[i];

		/* Allocate a buf ring */
		txr->buf_ring_size = adapter->buf_ring_size;
		txr->br = buf_ring_alloc(txr->buf_ring_size, M_DEVBUF, M_WAITOK,
			&txr->ring_mtx);

		/* Allocate Tx statistics. */
		ena_alloc_counters((counter_u64_t *)&txr->tx_stats,
		    sizeof(txr->tx_stats));
		txr->tx_last_cleanup_ticks = bsd_ticks;

		/* Allocate Rx statistics. */
		ena_alloc_counters((counter_u64_t *)&rxr->rx_stats,
		    sizeof(rxr->rx_stats));

		/* Initialize locks */
		snprintf(txr->mtx_name, nitems(txr->mtx_name), "ena:tx(%d)", i);
		snprintf(rxr->mtx_name, nitems(rxr->mtx_name), "ena:rx(%d)", i);

		mtx_init(&txr->ring_mtx, txr->mtx_name, NULL, MTX_DEF);
	}
}

static void
ena_init_io_rings(struct ena_adapter *adapter)
{
	/*
	 * IO rings initialization can be divided into the 2 steps:
	 *   1. Initialize variables and fields with initial values and copy
	 *      them from adapter/ena_dev (basic)
	 *   2. Allocate mutex, counters and buf_ring (advanced)
	 */
	ena_init_io_rings_basic(adapter);
	ena_init_io_rings_advanced(adapter);
}

static void
ena_free_io_ring_resources(struct ena_adapter *adapter, unsigned int qid)
{
	struct ena_ring *txr = &adapter->tx_ring[qid];
	struct ena_ring *rxr = &adapter->rx_ring[qid];

	ena_free_counters((counter_u64_t *)&txr->tx_stats,
	    sizeof(txr->tx_stats));
	ena_free_counters((counter_u64_t *)&rxr->rx_stats,
	    sizeof(rxr->rx_stats));

	ENA_RING_MTX_LOCK(txr);
	struct mbuf *m;
	while ((m = (struct mbuf *)buf_ring_dequeue_sc(txr->br)) != NULL)
		m_freem(m);
	buf_ring_free(txr->br, M_DEVBUF);
	ENA_RING_MTX_UNLOCK(txr);

	mtx_destroy(&txr->ring_mtx);
}

static void
ena_free_all_io_rings_resources(struct ena_adapter *adapter)
{
	int i;

	for (i = 0; i < adapter->num_io_queues; i++)
		ena_free_io_ring_resources(adapter, i);
}

TRACEPOINT(trace_ena_enqueue_wake, "");

static void
enqueue_work(ena_ring *ring)
{
	do {
		sched::thread::wait_for([ring] { return ring->enqueue_pending > 0 || ring->enqueue_stop; });
		ring->enqueue_pending = 0;

		if (!ring->enqueue_stop) {
			trace_ena_enqueue_wake();
			ena_deferred_mq_start(ring);
		}
	} while (!ring->enqueue_stop);
}

/**
 * ena_setup_tx_resources - allocate Tx resources (Descriptors)
 * @adapter: network interface device structure
 * @qid: queue index
 *
 * Returns 0 on success, otherwise on failure.
 **/
static int
ena_setup_tx_resources(struct ena_adapter *adapter, int qid)
{
	struct ena_que *que = &adapter->que[qid];
	struct ena_ring *tx_ring = que->tx_ring;
	int size, i;

	size = sizeof(struct ena_tx_buffer) * tx_ring->ring_size;

	tx_ring->tx_buffer_info = static_cast<ena_tx_buffer*>(aligned_alloc(alignof(ena_tx_buffer), size));
	if (unlikely(tx_ring->tx_buffer_info == NULL))
		return (ENOMEM);
	bzero(tx_ring->tx_buffer_info, size);

	size = sizeof(uint16_t) * tx_ring->ring_size;
	tx_ring->free_tx_ids = static_cast<uint16_t*>(malloc(size, M_DEVBUF, M_NOWAIT | M_ZERO));
	if (unlikely(tx_ring->free_tx_ids == NULL))
		goto err_buf_info_free;

	size = tx_ring->tx_max_header_size;
	tx_ring->push_buf_intermediate_buf = static_cast<uint8_t*>(malloc(size, M_DEVBUF,
	    M_NOWAIT | M_ZERO));
	if (unlikely(tx_ring->push_buf_intermediate_buf == NULL))
		goto err_tx_ids_free;

	/* Req id stack for TX OOO completions */
	for (i = 0; i < tx_ring->ring_size; i++)
		tx_ring->free_tx_ids[i] = i;

	/* Reset TX statistics. */
	ena_reset_counters((counter_u64_t *)&tx_ring->tx_stats,
	    sizeof(tx_ring->tx_stats));

	tx_ring->next_to_use = 0;
	tx_ring->next_to_clean = 0;
	tx_ring->acum_pkts = 0;

	/* Make sure that drbr is empty */
	ENA_RING_MTX_LOCK(tx_ring);
	struct mbuf *m;
	while ((m = (struct mbuf *)buf_ring_dequeue_sc(tx_ring->br)) != NULL)
		m_freem(m);
	ENA_RING_MTX_UNLOCK(tx_ring);

	/* Allocate taskqueues */
	tx_ring->enqueue_thread = sched::thread::make([tx_ring] { enqueue_work(tx_ring); },
		sched::thread::attr().name("ena_tx_enque_" + std::to_string(que->id)));
	tx_ring->enqueue_stop = false;
	tx_ring->enqueue_pending = 0;

	tx_ring->running = true;
	tx_ring->enqueue_thread->start();

	return (0);

err_tx_ids_free:
	free(tx_ring->free_tx_ids, M_DEVBUF);
	tx_ring->free_tx_ids = NULL;
err_buf_info_free:
	free(tx_ring->tx_buffer_info, M_DEVBUF);
	tx_ring->tx_buffer_info = NULL;

	return (ENOMEM);
}

/**
 * ena_free_tx_resources - Free Tx Resources per Queue
 * @adapter: network interface device structure
 * @qid: queue index
 *
 * Free all transmit software resources
 **/
static void
ena_free_tx_resources(struct ena_adapter *adapter, int qid)
{
	struct ena_ring *tx_ring = &adapter->tx_ring[qid];

	tx_ring->enqueue_thread->wake_with([tx_ring] { tx_ring->enqueue_stop = true; });
	tx_ring->enqueue_thread->join();

	delete tx_ring->enqueue_thread;
	tx_ring->enqueue_thread = nullptr;

	ENA_RING_MTX_LOCK(tx_ring);
	/* Flush buffer ring, */
	struct mbuf *m;
	while ((m = (struct mbuf *)buf_ring_dequeue_sc(tx_ring->br)) != NULL)
		m_freem(m);

	/* Free mbufs */
	for (int i = 0; i < tx_ring->ring_size; i++) {
		m_freem(tx_ring->tx_buffer_info[i].mbuf);
		tx_ring->tx_buffer_info[i].mbuf = NULL;
	}
	ENA_RING_MTX_UNLOCK(tx_ring);

	/* And free allocated memory. */
	free(tx_ring->tx_buffer_info, M_DEVBUF);
	tx_ring->tx_buffer_info = NULL;

	free(tx_ring->free_tx_ids, M_DEVBUF);
	tx_ring->free_tx_ids = NULL;

	free(tx_ring->push_buf_intermediate_buf, M_DEVBUF);
	tx_ring->push_buf_intermediate_buf = NULL;
}

/**
 * ena_setup_all_tx_resources - allocate all queues Tx resources
 * @adapter: network interface device structure
 *
 * Returns 0 on success, otherwise on failure.
 **/
static int
ena_setup_all_tx_resources(struct ena_adapter *adapter)
{
	int i, rc;

	for (i = 0; i < adapter->num_io_queues; i++) {
		rc = ena_setup_tx_resources(adapter, i);
		if (rc != 0) {
			ena_log(adapter->pdev, ERR,
			    "Allocation for Tx Queue %u failed", i);
			goto err_setup_tx;
		}
	}

	return (0);

err_setup_tx:
	/* Rewind the index freeing the rings as we go */
	while (i--)
		ena_free_tx_resources(adapter, i);
	return (rc);
}

/**
 * ena_free_all_tx_resources - Free Tx Resources for All Queues
 * @adapter: network interface device structure
 *
 * Free all transmit software resources
 **/
static void
ena_free_all_tx_resources(struct ena_adapter *adapter)
{
	int i;

	for (i = 0; i < adapter->num_io_queues; i++)
		ena_free_tx_resources(adapter, i);
}

/**
 * ena_setup_rx_resources - allocate Rx resources (Descriptors)
 * @adapter: network interface device structure
 * @qid: queue index
 *
 * Returns 0 on success, otherwise on failure.
 **/
static int
ena_setup_rx_resources(struct ena_adapter *adapter, unsigned int qid)
{
	struct ena_que *que = &adapter->que[qid];
	struct ena_ring *rx_ring = que->rx_ring;
	int size, i;

	size = sizeof(struct ena_rx_buffer) * rx_ring->ring_size;

	/*
	 * Alloc extra element so in rx path
	 * we can always prefetch rx_info + 1
	 */
	size += sizeof(struct ena_rx_buffer);

	rx_ring->rx_buffer_info = static_cast<ena_rx_buffer*>(aligned_alloc(alignof(ena_rx_buffer), size));
	bzero(rx_ring->rx_buffer_info, size);

	size = sizeof(uint16_t) * rx_ring->ring_size;
	rx_ring->free_rx_ids = static_cast<uint16_t*>(malloc(size, M_DEVBUF, M_WAITOK));

	for (i = 0; i < rx_ring->ring_size; i++)
		rx_ring->free_rx_ids[i] = i;

	/* Reset RX statistics. */
	ena_reset_counters((counter_u64_t *)&rx_ring->rx_stats,
	    sizeof(rx_ring->rx_stats));

	rx_ring->next_to_clean = 0;
	rx_ring->next_to_use = 0;

	/* Create LRO for the ring */
	if (adapter->ifp->if_capenable & IFCAP_LRO != 0) {
		int err = tcp_lro_init(&rx_ring->lro);
		if (err != 0) {
			ena_log(pdev, ERR, "LRO[%d] Initialization failed!",
			    qid);
		} else {
			ena_log(pdev, DBG, "RX Soft LRO[%d] Initialized",
			    qid);
			rx_ring->lro.ifp = adapter->ifp;
		}
	}

	return (0);
}

/**
 * ena_free_rx_resources - Free Rx Resources
 * @adapter: network interface device structure
 * @qid: queue index
 *
 * Free all receive software resources
 **/
static void
ena_free_rx_resources(struct ena_adapter *adapter, unsigned int qid)
{
	struct ena_ring *rx_ring = &adapter->rx_ring[qid];

	/* Free buffer DMA maps, */
	for (int i = 0; i < rx_ring->ring_size; i++) {
		m_freem(rx_ring->rx_buffer_info[i].mbuf);
		rx_ring->rx_buffer_info[i].mbuf = NULL;
	}

	/* free LRO resources, */
	tcp_lro_free(&rx_ring->lro);

	/* free allocated memory */
	free(rx_ring->rx_buffer_info, M_DEVBUF);
	rx_ring->rx_buffer_info = NULL;

	free(rx_ring->free_rx_ids, M_DEVBUF);
	rx_ring->free_rx_ids = NULL;
}

/**
 * ena_setup_all_rx_resources - allocate all queues Rx resources
 * @adapter: network interface device structure
 *
 * Returns 0 on success, otherwise on failure.
 **/
static int
ena_setup_all_rx_resources(struct ena_adapter *adapter)
{
	int i, rc = 0;

	for (i = 0; i < adapter->num_io_queues; i++) {
		rc = ena_setup_rx_resources(adapter, i);
		if (rc != 0) {
			ena_log(adapter->pdev, ERR,
			    "Allocation for Rx Queue %u failed", i);
			goto err_setup_rx;
		}
	}
	return (0);

err_setup_rx:
	/* rewind the index freeing the rings as we go */
	while (i--)
		ena_free_rx_resources(adapter, i);
	return (rc);
}

/**
 * ena_free_all_rx_resources - Free Rx resources for all queues
 * @adapter: network interface device structure
 *
 * Free all receive software resources
 **/
static void
ena_free_all_rx_resources(struct ena_adapter *adapter)
{
	int i;

	for (i = 0; i < adapter->num_io_queues; i++)
		ena_free_rx_resources(adapter, i);
}

static inline int
ena_alloc_rx_mbuf(struct ena_adapter *adapter, struct ena_ring *rx_ring,
    struct ena_rx_buffer *rx_info)
{
	struct ena_com_buf *ena_buf;
	int mlen;

	/* if previous allocated frag is not used */
	if (unlikely(rx_info->mbuf != NULL))
		return (0);

	/* Get mbuf using UMA allocator */
	rx_info->mbuf = m_getjcl(M_NOWAIT, MT_DATA, M_PKTHDR,
	    rx_ring->rx_mbuf_sz);

	if (unlikely(rx_info->mbuf == NULL)) {
		counter_u64_add(rx_ring->rx_stats.mjum_alloc_fail, 1);
		rx_info->mbuf = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
		if (unlikely(rx_info->mbuf == NULL)) {
			counter_u64_add(rx_ring->rx_stats.mbuf_alloc_fail, 1);
			return (ENOMEM);
		}
		mlen = MCLBYTES;
	} else {
		mlen = rx_ring->rx_mbuf_sz;
	}
	/* Set mbuf length*/
	rx_info->mbuf->M_dat.MH.MH_pkthdr.len = rx_info->mbuf->m_hdr.mh_len = mlen;

	ena_buf = &rx_info->ena_buf;
	ena_buf->paddr = mmu::virt_to_phys(rx_info->mbuf->m_hdr.mh_data);
	ena_buf->len = mlen;

	return (0);
}

static void
ena_free_rx_mbuf(struct ena_adapter *adapter, struct ena_ring *rx_ring,
    struct ena_rx_buffer *rx_info)
{
	if (rx_info->mbuf == NULL) {
		ena_log(adapter->pdev, WARN,
		    "Trying to free unallocated buffer");
		return;
	}

	m_freem(rx_info->mbuf);
	rx_info->mbuf = NULL;
}

/**
 * ena_refill_rx_bufs - Refills ring with descriptors
 * @rx_ring: the ring which we want to feed with free descriptors
 * @num: number of descriptors to refill
 * Refills the ring with newly allocated DMA-mapped mbufs for receiving
 **/
int
ena_refill_rx_bufs(struct ena_ring *rx_ring, uint32_t num)
{
	struct ena_adapter *adapter = rx_ring->adapter;
	uint16_t next_to_use, req_id;
	uint32_t i;
	int rc;

	ena_log_io(adapter->pdev, INFO, "refill qid: %d", rx_ring->qid);

	next_to_use = rx_ring->next_to_use;

	for (i = 0; i < num; i++) {
		struct ena_rx_buffer *rx_info;

		ena_log_io(pdev, DBG, "RX buffer - next to use: %d",
			next_to_use);

		req_id = rx_ring->free_rx_ids[next_to_use];
		rx_info = &rx_ring->rx_buffer_info[req_id];

		rc = ena_alloc_rx_mbuf(adapter, rx_ring, rx_info);
		if (unlikely(rc != 0)) {
			ena_log_io(pdev, WARN,
			    "failed to alloc buffer for rx queue %d",
			    rx_ring->qid);
			break;
		}
		rc = ena_com_add_single_rx_desc(rx_ring->ena_com_io_sq,
		    &rx_info->ena_buf, req_id);
		if (unlikely(rc != 0)) {
			ena_log_io(pdev, WARN,
			    "failed to add buffer for rx queue %d",
			    rx_ring->qid);
			break;
		}
		next_to_use = ENA_RX_RING_IDX_NEXT(next_to_use,
		    rx_ring->ring_size);
	}
	ena_log_io(pdev, INFO,
	    "allocated %d RX BUFs", num);

	if (unlikely(i < num)) {
		counter_u64_add(rx_ring->rx_stats.refil_partial, 1);
		ena_log_io(pdev, WARN,
		    "refilled rx qid %d with only %d mbufs (from %d)",
		    rx_ring->qid, i, num);
	}

	if (likely(i != 0))
		ena_com_write_sq_doorbell(rx_ring->ena_com_io_sq);

	rx_ring->next_to_use = next_to_use;
	return (i);
}

static void
ena_free_rx_bufs(struct ena_adapter *adapter, unsigned int qid)
{
	struct ena_ring *rx_ring = &adapter->rx_ring[qid];
	unsigned int i;

	for (i = 0; i < rx_ring->ring_size; i++) {
		struct ena_rx_buffer *rx_info = &rx_ring->rx_buffer_info[i];

		if (rx_info->mbuf != NULL)
			ena_free_rx_mbuf(adapter, rx_ring, rx_info);
	}
}

/**
 * ena_refill_all_rx_bufs - allocate all queues Rx buffers
 * @adapter: network interface device structure
 *
 */
static void
ena_refill_all_rx_bufs(struct ena_adapter *adapter)
{
	struct ena_ring *rx_ring;
	int i, rc, bufs_num;

	for (i = 0; i < adapter->num_io_queues; i++) {
		rx_ring = &adapter->rx_ring[i];
		bufs_num = rx_ring->ring_size - 1;
		rc = ena_refill_rx_bufs(rx_ring, bufs_num);
		if (unlikely(rc != bufs_num))
			ena_log_io(adapter->pdev, WARN,
			    "refilling Queue %d failed. "
			    "Allocated %d buffers from: %d",
			    i, rc, bufs_num);
	}
}

static void
ena_free_all_rx_bufs(struct ena_adapter *adapter)
{
	int i;

	for (i = 0; i < adapter->num_io_queues; i++)
		ena_free_rx_bufs(adapter, i);
}

/**
 * ena_free_tx_bufs - Free Tx Buffers per Queue
 * @adapter: network interface device structure
 * @qid: queue index
 **/
static void
ena_free_tx_bufs(struct ena_adapter *adapter, unsigned int qid)
{
	bool print_once = true;
	struct ena_ring *tx_ring = &adapter->tx_ring[qid];

	ENA_RING_MTX_LOCK(tx_ring);
	for (int i = 0; i < tx_ring->ring_size; i++) {
		struct ena_tx_buffer *tx_info = &tx_ring->tx_buffer_info[i];

		if (tx_info->mbuf == NULL)
			continue;

		if (print_once) {
			ena_log(adapter->pdev, WARN,
			    "free uncompleted tx mbuf qid %d idx 0x%x", qid,
			    i);
			print_once = false;
		} else {
			ena_log(adapter->pdev, DBG,
			    "free uncompleted tx mbuf qid %d idx 0x%x", qid,
			    i);
		}

		m_free(tx_info->mbuf);
		tx_info->mbuf = NULL;
	}
	ENA_RING_MTX_UNLOCK(tx_ring);
}

static void
ena_free_all_tx_bufs(struct ena_adapter *adapter)
{
	for (int i = 0; i < adapter->num_io_queues; i++)
		ena_free_tx_bufs(adapter, i);
}

static void
ena_destroy_all_tx_queues(struct ena_adapter *adapter)
{
	uint16_t ena_qid;
	int i;

	for (i = 0; i < adapter->num_io_queues; i++) {
		ena_qid = ENA_IO_TXQ_IDX(i);
		ena_com_destroy_io_queue(adapter->ena_dev, ena_qid);
	}
}

static void
ena_destroy_all_rx_queues(struct ena_adapter *adapter)
{
	uint16_t ena_qid;
	int i;

	for (i = 0; i < adapter->num_io_queues; i++) {
		ena_qid = ENA_IO_RXQ_IDX(i);
		ena_com_destroy_io_queue(adapter->ena_dev, ena_qid);
	}
}

static void
ena_destroy_all_io_queues(struct ena_adapter *adapter)
{
	struct ena_que *queue;

	for (int i = 0; i < adapter->num_io_queues; i++) {
		queue = &adapter->que[i];
		queue->cleanup_thread->wake_with([queue] { queue->cleanup_stop = true; });
	}

	for (int i = 0; i < adapter->num_io_queues; i++) {
		queue = &adapter->que[i];
		queue->cleanup_thread->join();

		delete queue->cleanup_thread;
		queue->cleanup_thread = nullptr;
	}

	ena_destroy_all_tx_queues(adapter);
	ena_destroy_all_rx_queues(adapter);
}

TRACEPOINT(trace_ena_cleanup_wake, "");

static void
cleanup_work(ena_que *queue)
{
	do {
		sched::thread::wait_for([queue] { return queue->cleanup_pending > 0 || queue->cleanup_stop; });
		queue->cleanup_pending = 0;

		ena_log(dev, DBG, "cleanup_work: received signal to cleanup queue %d", queue->id);
		if (!queue->cleanup_stop) {
			ena_log(dev, INFO, "cleanup_work: cleaning up queue %d", queue->id);
			ena_cleanup(queue);
			trace_ena_cleanup_wake();
		}
	} while (!queue->cleanup_stop);
}

static int
ena_create_io_queues(struct ena_adapter *adapter)
{
	struct ena_com_dev *ena_dev = adapter->ena_dev;
	struct ena_com_create_io_ctx ctx;
	struct ena_ring *ring;
	struct ena_que *queue;
	uint16_t ena_qid;
	uint32_t msix_vector;
	int rc, i;

	/* Create TX queues */
	for (i = 0; i < adapter->num_io_queues; i++) {
		msix_vector = ENA_IO_IRQ_IDX(i);
		ena_qid = ENA_IO_TXQ_IDX(i);
		ctx.mem_queue_type = ena_dev->tx_mem_queue_type;
		ctx.direction = ENA_COM_IO_QUEUE_DIRECTION_TX;
		ctx.queue_size = adapter->requested_tx_ring_size;
		ctx.msix_vector = msix_vector;
		ctx.qid = ena_qid;
		ctx.numa_node = adapter->que[i].domain;

		rc = ena_com_create_io_queue(ena_dev, &ctx);
		if (rc != 0) {
			ena_log(adapter->pdev, ERR,
			    "Failed to create io TX queue #%d rc: %d", i, rc);
			goto err_tx;
		}
		ring = &adapter->tx_ring[i];
		rc = ena_com_get_io_handlers(ena_dev, ena_qid,
		    &ring->ena_com_io_sq, &ring->ena_com_io_cq);
		if (rc != 0) {
			ena_log(adapter->pdev, ERR,
			    "Failed to get TX queue handlers. TX queue num"
			    " %d rc: %d",
			    i, rc);
			ena_com_destroy_io_queue(ena_dev, ena_qid);
			goto err_tx;
		}

		if (ctx.numa_node >= 0) {
			ena_com_update_numa_node(ring->ena_com_io_cq,
			    ctx.numa_node);
		}
	}

	/* Create RX queues */
	for (i = 0; i < adapter->num_io_queues; i++) {
		msix_vector = ENA_IO_IRQ_IDX(i);
		ena_qid = ENA_IO_RXQ_IDX(i);
		ctx.mem_queue_type = ENA_ADMIN_PLACEMENT_POLICY_HOST;
		ctx.direction = ENA_COM_IO_QUEUE_DIRECTION_RX;
		ctx.queue_size = adapter->requested_rx_ring_size;
		ctx.msix_vector = msix_vector;
		ctx.qid = ena_qid;
		ctx.numa_node = adapter->que[i].domain;

		rc = ena_com_create_io_queue(ena_dev, &ctx);
		if (unlikely(rc != 0)) {
			ena_log(adapter->pdev, ERR,
			    "Failed to create io RX queue[%d] rc: %d", i, rc);
			goto err_rx;
		}

		ring = &adapter->rx_ring[i];
		rc = ena_com_get_io_handlers(ena_dev, ena_qid,
		    &ring->ena_com_io_sq, &ring->ena_com_io_cq);
		if (unlikely(rc != 0)) {
			ena_log(adapter->pdev, ERR,
			    "Failed to get RX queue handlers. RX queue num"
			    " %d rc: %d",
			    i, rc);
			ena_com_destroy_io_queue(ena_dev, ena_qid);
			goto err_rx;
		}

		if (ctx.numa_node >= 0) {
			ena_com_update_numa_node(ring->ena_com_io_cq,
			    ctx.numa_node);
		}
	}

	for (i = 0; i < adapter->num_io_queues; i++) {
		queue = &adapter->que[i];

		//We pin each cleanup worker thread and corresponding MSIX vector
		//to one of the cpus (queue modulo #cpus) in order to minimize IPIs
		int cpu = i % sched::cpus.size();
		queue->cleanup_thread = sched::thread::make([queue] { cleanup_work(queue); },
			sched::thread::attr().name("ena_clean_que_" + std::to_string(i)).pin(sched::cpus[cpu]));
		queue->cleanup_thread->set_priority(sched::thread::priority_infinity);
		queue->cleanup_stop = false;
		queue->cleanup_pending = 0;
		queue->cleanup_thread->start();
	}

	return (0);

err_rx:
	while (i--)
		ena_com_destroy_io_queue(ena_dev, ENA_IO_RXQ_IDX(i));
	i = adapter->num_io_queues;
err_tx:
	while (i--)
		ena_com_destroy_io_queue(ena_dev, ENA_IO_TXQ_IDX(i));

	return (ENXIO);
}

/*********************************************************************
 *
 *  MSIX & Interrupt Service routine
 *
 **********************************************************************/

/**
 * ena_intr_msix_mgmnt - MSIX Interrupt Handler for admin/async queue
 * @arg: interrupt number
 **/
static void
ena_intr_msix_mgmnt(void *arg)
{
	struct ena_adapter *adapter = (struct ena_adapter *)arg;

	ena_com_admin_q_comp_intr_handler(adapter->ena_dev);
	if (likely(ENA_FLAG_ISSET(ENA_FLAG_DEVICE_RUNNING, adapter)))
		ena_com_aenq_intr_handler(adapter->ena_dev, arg);
}

/**
 * ena_handle_msix - MSIX Interrupt Handler for Tx/Rx
 * @arg: queue
 **/
static int
ena_handle_msix(void *arg)
{
	struct ena_que *queue = static_cast<ena_que*>(arg);
	struct ena_adapter *adapter = queue->adapter;
	if_t ifp = adapter->ifp;

	if (unlikely((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0))
		return 0;

	queue->cleanup_thread->wake_with_irq_or_preemption_disabled([queue] { queue->cleanup_pending++; });

	return 0;
}

static int
ena_enable_msix(struct ena_adapter *adapter)
{
	pci::device *dev = adapter->pdev;

	if (ENA_FLAG_ISSET(ENA_FLAG_MSIX_ENABLED, adapter)) {
		ena_log(dev, ERR, "Error, MSI-X is already enabled");
		return (EINVAL);
	}

	/* Reserved the max msix vectors we might need */
	int msix_vecs = ENA_MAX_MSIX_VEC(adapter->max_num_io_queues);

	ena_log(dev, DBG, "trying to enable MSI-X, vectors: %d", msix_vecs);

	dev->set_bus_master(true);
	dev->msix_enable();
	assert(dev->is_msix());

	if (msix_vecs != dev->msix_get_num_entries()) {
		if (msix_vecs == ENA_ADMIN_MSIX_VEC) {
			ena_log(dev, ERR,
			    "Not enough number of MSI-x allocated: %d",
			    msix_vecs);
			dev->msix_disable();
			return ENOSPC;
		}
		ena_log(dev, ERR,
		    "Enable only %d MSI-x (out of %d), reduce "
		    "the number of queues",
		    msix_vecs, dev->msix_get_num_entries());
	}

	adapter->msix_vecs = msix_vecs;
	ENA_FLAG_SET_ATOMIC(ENA_FLAG_MSIX_ENABLED, adapter);

	return (0);
}

static void
ena_setup_mgmnt_intr(struct ena_adapter *adapter)
{
	adapter->irq_tbl[ENA_MGMNT_IRQ_IDX].data = adapter;
	adapter->irq_tbl[ENA_MGMNT_IRQ_IDX].vector = ENA_MGMNT_IRQ_IDX;
	adapter->irq_tbl[ENA_MGMNT_IRQ_IDX].mvector = nullptr;
}

static int
ena_setup_io_intr(struct ena_adapter *adapter)
{
	for (int i = 0; i < adapter->num_io_queues; i++) {
		int irq_idx = ENA_IO_IRQ_IDX(i);

		adapter->irq_tbl[irq_idx].data = &adapter->que[i];
		adapter->irq_tbl[irq_idx].vector = irq_idx;
		adapter->irq_tbl[irq_idx].mvector = nullptr;

		adapter->que[i].domain = -1;
	}

	return (0);
}

static int
ena_request_mgmnt_irq(struct ena_adapter *adapter)
{
	interrupt_manager _msi(adapter->pdev);

	std::vector<msix_vector*> assigned = _msi.request_vectors(1);
	if (assigned.size() != 1) {
		_msi.free_vectors(assigned);
		ena_log(pdev, ERR, "could not request MGMNT irq vector: %d", ENA_MGMNT_IRQ_IDX);
		return (ENXIO);
	}

	auto vec = assigned[0];
	if (!_msi.assign_isr(vec, [adapter]() { ena_intr_msix_mgmnt(adapter); })) {
		_msi.free_vectors(assigned);
		ena_log(pdev, ERR, "could not assign MGMNT irq vector isr: %d", ENA_MGMNT_IRQ_IDX);
		return (ENXIO);
	}

	if (!_msi.setup_entry(ENA_MGMNT_IRQ_IDX, vec)) {
		_msi.free_vectors(assigned);
		ena_log(pdev, ERR, "could not setup MGMNT irq vector entry: %d", ENA_MGMNT_IRQ_IDX);
		return (ENXIO);
	}

	//Save assigned msix vector
	adapter->irq_tbl[ENA_MGMNT_IRQ_IDX].mvector = assigned[0];
	_msi.unmask_interrupts(assigned);

	return 0;
}

static int
ena_request_io_irq(struct ena_adapter *adapter)
{
	if (unlikely(!ENA_FLAG_ISSET(ENA_FLAG_MSIX_ENABLED, adapter))) {
		ena_log(pdev, ERR,
		    "failed to request I/O IRQ: MSI-X is not enabled");
		return (EINVAL);
	}

	interrupt_manager _msi(adapter->pdev);

	auto vec_num = adapter->msix_vecs - 1;
	std::vector<msix_vector*> assigned = _msi.request_vectors(vec_num);
	if (assigned.size() != vec_num) {
		_msi.free_vectors(assigned);
		ena_log(pdev, ERR, "could not request %d I/O irq vectors", vec_num);
		return (ENXIO);
	}

	for (int entry = ENA_IO_IRQ_FIRST_IDX; entry < adapter->msix_vecs; entry++) {
		auto idx = entry - ENA_IO_IRQ_FIRST_IDX;
		auto vec = assigned[idx];
		auto queue = &adapter->que[idx];
		if (!_msi.assign_isr(vec, [queue]() { ena_handle_msix(queue); })) {
			_msi.free_vectors(assigned);
			ena_log(pdev, ERR, "could not assign I/O irq vector isr: %d", entry);
			return (ENXIO);
		}

		if (!_msi.setup_entry(entry, vec)) {
			_msi.free_vectors(assigned);
			ena_log(pdev, ERR, "could not setup I/O irq vector entry: %d", entry);
			return (ENXIO);
		}
	}
	//
	//Save assigned msix vectors
	for (int entry = ENA_IO_IRQ_FIRST_IDX; entry < adapter->msix_vecs; entry++) {
		ena_irq *irq = &adapter->irq_tbl[entry];
		auto idx = entry - ENA_IO_IRQ_FIRST_IDX;
		auto vec = irq->mvector = assigned[idx];
		//In our case the worker threads are all pinned so we probably do not need
		//to re-pin the interrupt vector
		auto cpu = idx % sched::cpus.size();
		std::atomic_thread_fence(std::memory_order_seq_cst);
		vec->set_affinity(sched::cpus[cpu]);
		std::atomic_thread_fence(std::memory_order_seq_cst);

		ena_log(pdev, INFO, "pinned MSIX vector on queue %d - cpu %d\n", idx, cpu);
	}

	_msi.unmask_interrupts(assigned);

	return 0;
}

static void
ena_free_mgmnt_irq(struct ena_adapter *adapter)
{
	ena_irq *irq = &adapter->irq_tbl[ENA_MGMNT_IRQ_IDX];
	if (irq->mvector) {
		delete irq->mvector;
		irq->mvector = nullptr;
	}
}

static void
ena_free_io_irq(struct ena_adapter *adapter)
{
	for (int i = ENA_IO_IRQ_FIRST_IDX; i < adapter->msix_vecs; i++) {
		ena_irq *irq = &adapter->irq_tbl[i];
		if (irq->mvector) {
			delete irq->mvector;
			irq->mvector = nullptr;
		}
	}
}

static void
ena_free_irqs(struct ena_adapter *adapter)
{
	ena_free_io_irq(adapter);
	ena_free_mgmnt_irq(adapter);
	ena_disable_msix(adapter);
}

static void
ena_disable_msix(struct ena_adapter *adapter)
{
	if (ENA_FLAG_ISSET(ENA_FLAG_MSIX_ENABLED, adapter)) {
		ENA_FLAG_CLEAR_ATOMIC(ENA_FLAG_MSIX_ENABLED, adapter);
		adapter->pdev->msix_disable();
	}

	adapter->msix_vecs = 0;
}

static void
ena_unmask_all_io_irqs(struct ena_adapter *adapter)
{
	struct ena_com_io_cq *io_cq;
	struct ena_eth_io_intr_reg intr_reg;
	struct ena_ring *tx_ring;
	uint16_t ena_qid;
	int i;

	/* Unmask interrupts for all queues */
	for (i = 0; i < adapter->num_io_queues; i++) {
		ena_qid = ENA_IO_TXQ_IDX(i);
		io_cq = &adapter->ena_dev->io_cq_queues[ena_qid];
		ena_com_update_intr_reg(&intr_reg, 0, 0, true);
		tx_ring = &adapter->tx_ring[i];
		counter_u64_add(tx_ring->tx_stats.unmask_interrupt_num, 1);
		ena_com_unmask_intr(io_cq, &intr_reg);
	}
}

static int
ena_up_complete(struct ena_adapter *adapter)
{
	int rc = ena_change_mtu(adapter->ifp, adapter->ifp->if_mtu);
	if (unlikely(rc != 0))
		return (rc);

	ena_refill_all_rx_bufs(adapter);
	ena_reset_counters((counter_u64_t *)&adapter->hw_stats,
	    sizeof(adapter->hw_stats));

	return (0);
}

static void
set_io_rings_size(struct ena_adapter *adapter, int new_tx_size, int new_rx_size)
{
	int i;

	for (i = 0; i < adapter->num_io_queues; i++) {
		adapter->tx_ring[i].ring_size = new_tx_size;
		adapter->rx_ring[i].ring_size = new_rx_size;
	}
}

static int
create_queues_with_size_backoff(struct ena_adapter *adapter)
{
	int rc;
	uint32_t cur_rx_ring_size, cur_tx_ring_size;
	uint32_t new_rx_ring_size, new_tx_ring_size;

	/*
	 * Current queue sizes might be set to smaller than the requested
	 * ones due to past queue allocation failures.
	 */
	set_io_rings_size(adapter, adapter->requested_tx_ring_size,
	    adapter->requested_rx_ring_size);

	while (1) {
		/* Allocate transmit descriptors */
		rc = ena_setup_all_tx_resources(adapter);
		if (unlikely(rc != 0)) {
			ena_log(pdev, ERR, "err_setup_tx");
			goto err_setup_tx;
		}

		/* Allocate receive descriptors */
		rc = ena_setup_all_rx_resources(adapter);
		if (unlikely(rc != 0)) {
			ena_log(pdev, ERR, "err_setup_rx");
			goto err_setup_rx;
		}

		/* Create IO queues for Rx & Tx */
		rc = ena_create_io_queues(adapter);
		if (unlikely(rc != 0)) {
			ena_log(pdev, ERR, "create IO queues failed");
			goto err_io_que;
		}

		return (0);

err_io_que:
		ena_free_all_rx_resources(adapter);
err_setup_rx:
		ena_free_all_tx_resources(adapter);
err_setup_tx:
		/*
		 * Lower the ring size if ENOMEM. Otherwise, return the
		 * error straightaway.
		 */
		if (unlikely(rc != ENOMEM)) {
			ena_log(pdev, ERR,
			    "Queue creation failed with error code: %d", rc);
			return (rc);
		}

		cur_tx_ring_size = adapter->tx_ring[0].ring_size;
		cur_rx_ring_size = adapter->rx_ring[0].ring_size;

		ena_log(pdev, ERR,
		    "Not enough memory to create queues with sizes TX=%d, RX=%d",
		    cur_tx_ring_size, cur_rx_ring_size);

		new_tx_ring_size = cur_tx_ring_size;
		new_rx_ring_size = cur_rx_ring_size;

		/*
		 * Decrease the size of a larger queue, or decrease both if they
		 * are the same size.
		 */
		if (cur_rx_ring_size <= cur_tx_ring_size)
			new_tx_ring_size = cur_tx_ring_size / 2;
		if (cur_rx_ring_size >= cur_tx_ring_size)
			new_rx_ring_size = cur_rx_ring_size / 2;

		if (new_tx_ring_size < ENA_MIN_RING_SIZE ||
		    new_rx_ring_size < ENA_MIN_RING_SIZE) {
			ena_log(pdev, ERR,
			    "Queue creation failed with the smallest possible queue size"
			    "of %d for both queues. Not retrying with smaller queues",
			    ENA_MIN_RING_SIZE);
			return (rc);
		}

		ena_log(pdev, INFO,
		    "Retrying queue creation with sizes TX=%d, RX=%d",
		    new_tx_ring_size, new_rx_ring_size);

		set_io_rings_size(adapter, new_tx_ring_size, new_rx_ring_size);
	}
}

int
ena_up(struct ena_adapter *adapter)
{
	int rc = 0;

	ENA_LOCK_ASSERT();

	if (ENA_FLAG_ISSET(ENA_FLAG_DEV_UP, adapter))
		return (0);

	ena_log(adapter->pdev, INFO, "device is going UP");

	/* setup interrupts for IO queues */
	rc = ena_setup_io_intr(adapter);
	if (unlikely(rc != 0)) {
		ena_log(adapter->pdev, ERR, "error setting up IO interrupt");
		goto error;
	}
	rc = ena_request_io_irq(adapter);
	if (unlikely(rc != 0)) {
		ena_log(adapter->pdev, ERR, "err_req_irq");
		goto error;
	}

	ena_log(adapter->pdev, INFO,
	    "Creating %u IO queues. Rx queue size: %d, Tx queue size: %d, LLQ is %s",
	    adapter->num_io_queues,
	    adapter->requested_rx_ring_size,
	    adapter->requested_tx_ring_size,
	    (adapter->ena_dev->tx_mem_queue_type ==
		ENA_ADMIN_PLACEMENT_POLICY_DEV) ? "ENABLED" : "DISABLED");

	rc = create_queues_with_size_backoff(adapter);
	if (unlikely(rc != 0)) {
		ena_log(adapter->pdev, ERR,
		    "error creating queues with size backoff");
		goto err_create_queues_with_backoff;
	}

	if (ENA_FLAG_ISSET(ENA_FLAG_LINK_UP, adapter))
		adapter->ifp->if_link_state = LINK_STATE_UP;

	rc = ena_up_complete(adapter);
	if (unlikely(rc != 0))
		goto err_up_complete;

	counter_u64_add(adapter->dev_stats.interface_up, 1);

	ena_update_hwassist(adapter);

	adapter->ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
	adapter->ifp->if_drv_flags |= IFF_DRV_RUNNING;

	ENA_FLAG_SET_ATOMIC(ENA_FLAG_DEV_UP, adapter);

	ena_unmask_all_io_irqs(adapter);

	return (0);

err_up_complete:
	ena_destroy_all_io_queues(adapter);
	ena_free_all_rx_resources(adapter);
	ena_free_all_tx_resources(adapter);
err_create_queues_with_backoff:
	ena_free_io_irq(adapter);
error:
	return (rc);
}

static void
ena_init(void *arg)
{
	struct ena_adapter *adapter = (struct ena_adapter *)arg;

	if (!ENA_FLAG_ISSET(ENA_FLAG_DEV_UP, adapter)) {
		ENA_LOCK_LOCK();
		ena_up(adapter);
		ENA_LOCK_UNLOCK();
	}
}

static int
ena_ioctl(if_t ifp, u_long command, caddr_t data)
{
	int rc = 0;

	switch (command) {
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			ifp->if_drv_flags |= IFF_DRV_RUNNING;
	                ena_log(adapter->pdev, INFO, "device is UP");
		} else {
			ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
	                ena_log(adapter->pdev, INFO, "device is DOWN");
		}
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		break;

	default:
		rc = ether_ioctl(ifp, command, data);
		break;
	}

	return (rc);
}

static int
ena_get_dev_offloads(struct ena_com_dev_get_features_ctx *feat)
{
	int caps = 0;

	if ((feat->offload.tx &
	    (ENA_ADMIN_FEATURE_OFFLOAD_DESC_TX_L4_IPV4_CSUM_FULL_MASK |
	    ENA_ADMIN_FEATURE_OFFLOAD_DESC_TX_L4_IPV4_CSUM_PART_MASK |
	    ENA_ADMIN_FEATURE_OFFLOAD_DESC_TX_L3_CSUM_IPV4_MASK)) != 0)
		caps |= IFCAP_TXCSUM;

	if ((feat->offload.tx &
	    (ENA_ADMIN_FEATURE_OFFLOAD_DESC_TX_L4_IPV6_CSUM_FULL_MASK |
	    ENA_ADMIN_FEATURE_OFFLOAD_DESC_TX_L4_IPV6_CSUM_PART_MASK)) != 0)
		caps |= IFCAP_TXCSUM_IPV6;

	if ((feat->offload.tx & ENA_ADMIN_FEATURE_OFFLOAD_DESC_TSO_IPV4_MASK) != 0)
		caps |= IFCAP_TSO4;

	if ((feat->offload.tx & ENA_ADMIN_FEATURE_OFFLOAD_DESC_TSO_IPV6_MASK) != 0)
		caps |= IFCAP_TSO6;

	if ((feat->offload.rx_supported &
	    (ENA_ADMIN_FEATURE_OFFLOAD_DESC_RX_L4_IPV4_CSUM_MASK |
	    ENA_ADMIN_FEATURE_OFFLOAD_DESC_RX_L3_CSUM_IPV4_MASK)) != 0)
		caps |= IFCAP_RXCSUM;

	if ((feat->offload.rx_supported &
	    ENA_ADMIN_FEATURE_OFFLOAD_DESC_RX_L4_IPV6_CSUM_MASK) != 0)
		caps |= IFCAP_RXCSUM_IPV6;

	caps |= IFCAP_LRO | IFCAP_JUMBO_MTU;

	ena_log(nullptr, INFO,
		"device offloads (caps): TXCSUM=%d, TXCSUM_IPV6=%d, TSO4=%d, TSO6=%d, RXCSUM=%d, RXCSUM_IPV6=%d, LRO=1, JUMBO_MTU=1\n",
		caps & IFCAP_TXCSUM, caps & IFCAP_TXCSUM_IPV6, caps & IFCAP_TSO4, caps & IFCAP_TSO6, caps & IFCAP_RXCSUM, caps & IFCAP_RXCSUM_IPV6);

	return (caps);
}

static void
ena_update_hwassist(struct ena_adapter *adapter)
{
	if_t ifp = adapter->ifp;
	uint32_t feat = adapter->tx_offload_cap;
	int cap = ifp->if_capenable;
	int flags = 0;

        //TODO: Revisit if all this flag setting logic makes sense
	if ((cap & IFCAP_TXCSUM) != 0) {
		if ((feat &
		    ENA_ADMIN_FEATURE_OFFLOAD_DESC_TX_L3_CSUM_IPV4_MASK) != 0)
			flags |= CSUM_IP;
		if ((feat &
		    (ENA_ADMIN_FEATURE_OFFLOAD_DESC_TX_L4_IPV4_CSUM_FULL_MASK |
		    ENA_ADMIN_FEATURE_OFFLOAD_DESC_TX_L4_IPV4_CSUM_PART_MASK)) != 0)
			flags |= CSUM_UDP | CSUM_TCP;
	}

	if ((cap & IFCAP_TXCSUM_IPV6) != 0)
		flags |= CSUM_UDP_IPV6 | CSUM_TCP_IPV6;

	if ((cap & IFCAP_TSO4) != 0)
		flags |= CSUM_TSO;

	if ((cap & IFCAP_TSO6) != 0)
		flags |= CSUM_TSO;

	adapter->ifp->if_hwassist = flags;

	ena_log(nullptr, INFO,
		"ena_update_hwassist: CSUM_IP=%d, CSUM_UDP=%d, CSUM_TCP=%d, CSUM_UDP_IPV6=%d, CSUM_TCP_IPV6=%d, CSUM_TSO=%d\n",
		flags & CSUM_IP, flags & CSUM_UDP, flags & CSUM_TCP, flags & CSUM_UDP_IPV6, flags & CSUM_TCP_IPV6, flags & CSUM_TSO);
}

static int
ena_setup_ifnet(pci::device *pdev, struct ena_adapter *adapter,
    struct ena_com_dev_get_features_ctx *feat)
{
	int caps = 0;

	if_t ifp = adapter->ifp = if_alloc(IFT_ETHER);
	if (unlikely(ifp == NULL)) {
		ena_log(pdev, ERR, "can not allocate ifnet structure");
		return (ENXIO);
	}
	if_initname(ifp, "eth", 0); //Eventually increment some instance ID

	ifp->if_softc = adapter;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST; //What about simplex?
	ifp->if_init = ena_init;
	ifp->if_transmit = ena_mq_start;
	ifp->if_qflush = ena_qflush;
	ifp->if_ioctl = ena_ioctl;

	IFQ_SET_MAXLEN(&ifp->if_snd, adapter->requested_tx_ring_size);
        //OSv FreeBSD version does not seem to implement ALTQ ("ALTernate Queueing") so ignore below
        //See https://www.usenix.org/legacy/publications/library/proceedings/lisa97/failsafe/usenix98/full_papers/cho/cho_html/cho.html
	//if_setsendqready(ifp); //This actually make this interface ALTQ-ready
	ifp->if_mtu = ETHERMTU;
	ifp->if_baudrate = 0;
	/* Zeroize capabilities... */
	ifp->if_capabilities = 0;
	ifp->if_capenable = 0;
	/* check hardware support */
	caps = ena_get_dev_offloads(feat);
	/* ... and set them */
	ifp->if_capabilities = caps;

	/* TSO parameters - NOT supported by this version of FreeBSD */
	//if_sethwtsomax(ifp, ENA_TSO_MAXSIZE - (ETHER_HDR_LEN + ETHER_VLAN_ENCAP_LEN));
	//if_sethwtsomaxsegcount(ifp, adapter->max_tx_sgl_size - 1);
	//if_sethwtsomaxsegsize(ifp, ENA_TSO_MAXSIZE);

	ifp->if_hdrlen = sizeof(struct ether_vlan_header); //Other OSv drivers do not set it, is it OK?
        ifp->if_capenable = ifp->if_capabilities; // | IFCAP_HWSTATS; //This to collect statistics - shall we set it like other drivers do?

	ether_ifattach(ifp, adapter->mac_addr);

	return (0);
}

void
ena_down(struct ena_adapter *adapter)
{
	int rc;

	ENA_LOCK_ASSERT();

	if (!ENA_FLAG_ISSET(ENA_FLAG_DEV_UP, adapter))
		return;

	ena_log(adapter->pdev, INFO, "device is going DOWN");

	ENA_FLAG_CLEAR_ATOMIC(ENA_FLAG_DEV_UP, adapter);
	adapter->ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
	adapter->ifp->if_drv_flags |= IFF_DRV_OACTIVE;

	ena_free_io_irq(adapter);

	if (ENA_FLAG_ISSET(ENA_FLAG_TRIGGER_RESET, adapter)) {
		rc = ena_com_dev_reset(adapter->ena_dev, adapter->reset_reason);
		if (unlikely(rc != 0))
			ena_log(adapter->pdev, ERR, "Device reset failed");
	}

	ena_destroy_all_io_queues(adapter);

	ena_free_all_tx_bufs(adapter);
	ena_free_all_rx_bufs(adapter);
	ena_free_all_tx_resources(adapter);
	ena_free_all_rx_resources(adapter);

	counter_u64_add(adapter->dev_stats.interface_down, 1);
}

static uint32_t
ena_calc_max_io_queue_num(pci::device *pdev, struct ena_com_dev *ena_dev,
    struct ena_com_dev_get_features_ctx *get_feat_ctx)
{
	uint32_t io_tx_sq_num, io_tx_cq_num, io_rx_num, max_num_io_queues;

	/* Regular queues capabilities */
	if (ena_dev->supported_features & BIT(ENA_ADMIN_MAX_QUEUES_EXT)) {
		struct ena_admin_queue_ext_feature_fields *max_queue_ext =
		    &get_feat_ctx->max_queue_ext.max_queue_ext;
		io_rx_num = min_t(int, max_queue_ext->max_rx_sq_num,
		    max_queue_ext->max_rx_cq_num);

		io_tx_sq_num = max_queue_ext->max_tx_sq_num;
		io_tx_cq_num = max_queue_ext->max_tx_cq_num;
	} else {
		struct ena_admin_queue_feature_desc *max_queues =
		    &get_feat_ctx->max_queues;
		io_tx_sq_num = max_queues->max_sq_num;
		io_tx_cq_num = max_queues->max_cq_num;
		io_rx_num = min_t(int, io_tx_sq_num, io_tx_cq_num);
	}

	max_num_io_queues = min_t(uint32_t, mp_ncpus, ENA_MAX_NUM_IO_QUEUES);
	max_num_io_queues = min_t(uint32_t, max_num_io_queues, io_rx_num);
	max_num_io_queues = min_t(uint32_t, max_num_io_queues, io_tx_sq_num);
	max_num_io_queues = min_t(uint32_t, max_num_io_queues, io_tx_cq_num);
	/* 1 IRQ for mgmnt and 1 IRQ for each TX/RX pair */
	max_num_io_queues = min_t(uint32_t, max_num_io_queues, pdev->msix_get_num_entries() - 1);

	return (max_num_io_queues);
}

static int
ena_set_queues_placement_policy(pci::device *pdev, struct ena_com_dev *ena_dev,
    struct ena_admin_feature_llq_desc *llq,
    struct ena_llq_configurations *llq_default_configurations)
{
	//We do NOT support LLQ
	ena_dev->tx_mem_queue_type = ENA_ADMIN_PLACEMENT_POLICY_HOST;

	ena_log(pdev, INFO,
		"LLQ is not supported. Using the host mode policy.");
	return (0);
}

//Originates from FreeBSD
static __inline __pure2 int
flsl(long mask)
{
	return (mask == 0 ? 0 :
	    8 * sizeof(mask) - __builtin_clzl((u_long)mask));
}

static int
ena_calc_io_queue_size(struct ena_calc_queue_size_ctx *ctx)
{
	struct ena_com_dev *ena_dev = ctx->ena_dev;
	uint32_t tx_queue_size = ENA_DEFAULT_RING_SIZE;
	uint32_t rx_queue_size = ENA_DEFAULT_RING_SIZE;
	uint32_t max_tx_queue_size;
	uint32_t max_rx_queue_size;

	if (ena_dev->supported_features & BIT(ENA_ADMIN_MAX_QUEUES_EXT)) {
		struct ena_admin_queue_ext_feature_fields *max_queue_ext =
		    &ctx->get_feat_ctx->max_queue_ext.max_queue_ext;
		max_rx_queue_size = min_t(uint32_t,
		    max_queue_ext->max_rx_cq_depth,
		    max_queue_ext->max_rx_sq_depth);
		max_tx_queue_size = max_queue_ext->max_tx_cq_depth;

		max_tx_queue_size = min_t(uint32_t, max_tx_queue_size,
		    max_queue_ext->max_tx_sq_depth);

		ctx->max_tx_sgl_size = min_t(uint16_t, ENA_PKT_MAX_BUFS,
		    max_queue_ext->max_per_packet_tx_descs);
		ctx->max_rx_sgl_size = min_t(uint16_t, ENA_PKT_MAX_BUFS,
		    max_queue_ext->max_per_packet_rx_descs);
	} else {
		struct ena_admin_queue_feature_desc *max_queues =
		    &ctx->get_feat_ctx->max_queues;
		max_rx_queue_size = min_t(uint32_t, max_queues->max_cq_depth,
		    max_queues->max_sq_depth);
		max_tx_queue_size = max_queues->max_cq_depth;

		max_tx_queue_size = min_t(uint32_t, max_tx_queue_size,
		    max_queues->max_sq_depth);

		ctx->max_tx_sgl_size = min_t(uint16_t, ENA_PKT_MAX_BUFS,
		    max_queues->max_packet_tx_descs);
		ctx->max_rx_sgl_size = min_t(uint16_t, ENA_PKT_MAX_BUFS,
		    max_queues->max_packet_rx_descs);
	}

	/* round down to the nearest power of 2 */
	max_tx_queue_size = 1 << (flsl(max_tx_queue_size) - 1);
	max_rx_queue_size = 1 << (flsl(max_rx_queue_size) - 1);

	tx_queue_size = clamp_val(tx_queue_size, ENA_MIN_RING_SIZE,
	    max_tx_queue_size);
	rx_queue_size = clamp_val(rx_queue_size, ENA_MIN_RING_SIZE,
	    max_rx_queue_size);

	tx_queue_size = 1 << (flsl(tx_queue_size) - 1);
	rx_queue_size = 1 << (flsl(rx_queue_size) - 1);

	ctx->max_tx_queue_size = max_tx_queue_size;
	ctx->max_rx_queue_size = max_rx_queue_size;
	ctx->tx_queue_size = tx_queue_size;
	ctx->rx_queue_size = rx_queue_size;

	return (0);
}

static void
ena_config_host_info(struct ena_com_dev *ena_dev, pci::device* dev)
{
	struct ena_admin_host_info *host_info;
	int rc;

	/* Allocate only the host info */
	rc = ena_com_allocate_host_info(ena_dev);
	if (unlikely(rc != 0)) {
		ena_log(dev, ERR, "Cannot allocate host info");
		return;
	}

	host_info = ena_dev->host_attr.host_info;

	u8 bus, slot, func;
	dev->get_bdf(bus, slot, func);
	host_info->bdf = (bus << 8) | (slot << 3) | func;

	host_info->os_type = ENA_ADMIN_OS_LINUX;
	host_info->kernel_ver = 0;

	//Maybe down the road put some OSv specific info
	host_info->kernel_ver_str[0] = '\0';
	host_info->os_dist = 0;
	host_info->os_dist_str[0] = '\0';

	host_info->driver_version = (ENA_DRV_MODULE_VER_MAJOR) |
	    (ENA_DRV_MODULE_VER_MINOR << ENA_ADMIN_HOST_INFO_MINOR_SHIFT) |
	    (ENA_DRV_MODULE_VER_SUBMINOR << ENA_ADMIN_HOST_INFO_SUB_MINOR_SHIFT);
	host_info->num_cpus = mp_ncpus;
	host_info->driver_supported_features = ENA_ADMIN_HOST_INFO_RX_OFFSET_MASK;

	rc = ena_com_set_host_attributes(ena_dev);
	if (unlikely(rc != 0)) {
		if (rc == EOPNOTSUPP)
			ena_log(dev, WARN, "Cannot set host attributes");
		else
			ena_log(dev, ERR, "Cannot set host attributes");

		goto err;
	}

	return;

err:
	ena_com_delete_host_info(ena_dev);
}

static int
ena_enable_msix_and_set_admin_interrupts(struct ena_adapter *adapter)
{
	struct ena_com_dev *ena_dev = adapter->ena_dev;
	int rc;

	rc = ena_enable_msix(adapter);
	if (unlikely(rc != 0)) {
		ena_log(adapter->pdev, ERR, "Error with MSI-X enablement");
		return (rc);
	}

	ena_setup_mgmnt_intr(adapter);

	rc = ena_request_mgmnt_irq(adapter);
	if (unlikely(rc != 0)) {
		ena_log(adapter->pdev, ERR, "Cannot setup mgmnt queue intr");
		goto err_disable_msix;
	}

	ena_com_set_admin_polling_mode(ena_dev, false);

	ena_com_admin_aenq_enable(ena_dev);

	return (0);

err_disable_msix:
	ena_disable_msix(adapter);

	return (rc);
}

/* Function called on ENA_ADMIN_KEEP_ALIVE event */
static void
ena_keep_alive_wd(void *adapter_data, struct ena_admin_aenq_entry *aenq_e)
{
	struct ena_adapter *adapter = (struct ena_adapter *)adapter_data;
	struct ena_admin_aenq_keep_alive_desc *desc;
	uint64_t rx_drops;
	uint64_t tx_drops;

	desc = (struct ena_admin_aenq_keep_alive_desc *)aenq_e;

	rx_drops = ((uint64_t)desc->rx_drops_high << 32) | desc->rx_drops_low;
	tx_drops = ((uint64_t)desc->tx_drops_high << 32) | desc->tx_drops_low;
	counter_u64_zero(adapter->hw_stats.rx_drops);
	counter_u64_add(adapter->hw_stats.rx_drops, rx_drops);
	counter_u64_zero(adapter->hw_stats.tx_drops);
	counter_u64_add(adapter->hw_stats.tx_drops, tx_drops);

	u64 uptime = osv::clock::uptime::now().time_since_epoch().count();
	adapter->keep_alive_timestamp.store(uptime, std::memory_order_release);
}

/* Check for keep alive expiration */
static void
check_for_missing_keep_alive(struct ena_adapter *adapter)
{
	if (adapter->wd_active == 0)
		return;

	if (adapter->keep_alive_timeout == ENA_HW_HINTS_NO_TIMEOUT)
		return;

	u64 timestamp = adapter->keep_alive_timestamp.load(std::memory_order_acquire);
	u64 now = osv::clock::uptime::now().time_since_epoch().count() - timestamp;
	if (unlikely(now > timestamp + adapter->keep_alive_timeout)) {
		ena_log(adapter->pdev, ERR, "Keep alive watchdog timeout.");
		counter_u64_add(adapter->dev_stats.wd_expired, 1);
		ena_trigger_reset(adapter, ENA_REGS_RESET_KEEP_ALIVE_TO);
	}
}

/* Check if admin queue is enabled */
static void
check_for_admin_com_state(struct ena_adapter *adapter)
{
	if (unlikely(ena_com_get_admin_running_state(adapter->ena_dev) == false)) {
		ena_log(adapter->pdev, ERR,
		    "ENA admin queue is not in running state!");
		counter_u64_add(adapter->dev_stats.admin_q_pause, 1);
		ena_trigger_reset(adapter, ENA_REGS_RESET_ADMIN_TO);
	}
}

static int
check_for_rx_interrupt_queue(struct ena_adapter *adapter,
    struct ena_ring *rx_ring)
{
	if (rx_ring->first_interrupt.load())
		return (0);

	if (ena_com_cq_empty(rx_ring->ena_com_io_cq))
		return (0);

	rx_ring->no_interrupt_event_cnt++;

	if (rx_ring->no_interrupt_event_cnt ==
	    ENA_MAX_NO_INTERRUPT_ITERATIONS) {
		ena_log(adapter->pdev, ERR,
		    "Potential MSIX issue on Rx side Queue = %d. Reset the device", rx_ring->qid);
		ena_trigger_reset(adapter, ENA_REGS_RESET_MISS_INTERRUPT);
		return (EIO);
	}

	return (0);
}

static int
check_missing_comp_in_tx_queue(struct ena_adapter *adapter,
    struct ena_ring *tx_ring)
{
	struct ena_tx_buffer *tx_buf;
	uint32_t missed_tx = 0;
	int i, rc = 0;

	u64 curtime = osv::clock::uptime::now().time_since_epoch().count();

	for (i = 0; i < tx_ring->ring_size; i++) {
		tx_buf = &tx_ring->tx_buffer_info[i];

		if (tx_buf->timestamp == 0)
			continue;

		u64 time_offset = curtime - tx_buf->timestamp;

		if (unlikely(!tx_ring->first_interrupt.load() &&
			time_offset > 2 * adapter->missing_tx_timeout)) {
			/*
			 * If after graceful period interrupt is still not
			 * received, we schedule a reset.
			 */
			ena_log(pdev, ERR,
			    "Potential MSIX issue on Tx side Queue = %d. "
			    "Reset the device",
			    tx_ring->qid);
			ena_trigger_reset(adapter,
			    ENA_REGS_RESET_MISS_INTERRUPT);
			return (EIO);
		}

		/* Check again if packet is still waiting */
		if (unlikely(time_offset > adapter->missing_tx_timeout)) {

			if (tx_buf->print_once) {
				ena_log(pdev, WARN,
				    "Found a Tx that wasn't completed on time, qid %d, index %d.",
				    tx_ring->qid, i);
			}

			tx_buf->print_once = false;
			missed_tx++;
		}
	}

	if (unlikely(missed_tx > adapter->missing_tx_threshold)) {
		ena_log(pdev, ERR,
		    "The number of lost tx completion is above the threshold "
		    "(%d > %d). Reset the device",
		    missed_tx, adapter->missing_tx_threshold);
		ena_trigger_reset(adapter, ENA_REGS_RESET_MISS_TX_CMPL);
		rc = EIO;
	}

	counter_u64_add(tx_ring->tx_stats.missing_tx_comp, missed_tx);

	return (rc);
}

/*
 * Check for TX which were not completed on time.
 * Timeout is defined by "missing_tx_timeout".
 * Reset will be performed if number of incompleted
 * transactions exceeds "missing_tx_threshold".
 */
static void
check_for_missing_completions(struct ena_adapter *adapter)
{
	struct ena_ring *tx_ring;
	struct ena_ring *rx_ring;
	int i, budget, rc;

	/* Make sure the driver doesn't turn the device in other process */
	rmb();

	if (!ENA_FLAG_ISSET(ENA_FLAG_DEV_UP, adapter))
		return;

	if (ENA_FLAG_ISSET(ENA_FLAG_TRIGGER_RESET, adapter))
		return;

	if (adapter->missing_tx_timeout == ENA_HW_HINTS_NO_TIMEOUT)
		return;

	budget = adapter->missing_tx_max_queues;

	for (i = adapter->next_monitored_tx_qid; i < adapter->num_io_queues; i++) {
		tx_ring = &adapter->tx_ring[i];
		rx_ring = &adapter->rx_ring[i];

		rc = check_missing_comp_in_tx_queue(adapter, tx_ring);
		if (unlikely(rc != 0))
			return;

		rc = check_for_rx_interrupt_queue(adapter, rx_ring);
		if (unlikely(rc != 0))
			return;

		budget--;
		if (budget == 0) {
			i++;
			break;
		}
	}

	adapter->next_monitored_tx_qid = i % adapter->num_io_queues;
}

/* trigger rx cleanup after 2 consecutive detections */
#define EMPTY_RX_REFILL 2
/* For the rare case where the device runs out of Rx descriptors and the
 * msix handler failed to refill new Rx descriptors (due to a lack of memory
 * for example).
 * This case will lead to a deadlock:
 * The device won't send interrupts since all the new Rx packets will be dropped
 * The msix handler won't allocate new Rx descriptors so the device won't be
 * able to send new packets.
 *
 * When such a situation is detected - execute rx cleanup task in another thread
 */
static void
check_for_empty_rx_ring(struct ena_adapter *adapter)
{
	struct ena_ring *rx_ring;
	int i, refill_required;

	if (!ENA_FLAG_ISSET(ENA_FLAG_DEV_UP, adapter))
		return;

	if (ENA_FLAG_ISSET(ENA_FLAG_TRIGGER_RESET, adapter))
		return;

	for (i = 0; i < adapter->num_io_queues; i++) {
		rx_ring = &adapter->rx_ring[i];

		refill_required = ena_com_free_q_entries(
		    rx_ring->ena_com_io_sq);
		if (unlikely(refill_required == (rx_ring->ring_size - 1))) {
			rx_ring->empty_rx_queue++;

			if (rx_ring->empty_rx_queue >= EMPTY_RX_REFILL) {
				counter_u64_add(rx_ring->rx_stats.empty_rx_ring, 1);

				ena_log(adapter->pdev, WARN,
				    "Rx ring %d is stalled. Triggering the refill function",
				    i);

				auto queue = rx_ring->que;
				rx_ring->que->cleanup_thread->wake_with([queue] { queue->cleanup_pending++; });
				rx_ring->empty_rx_queue = 0;
			}
		} else {
			rx_ring->empty_rx_queue = 0;
		}
	}
}

static void
ena_update_hints(struct ena_adapter *adapter,
    struct ena_admin_ena_hw_hints *hints)
{
	struct ena_com_dev *ena_dev = adapter->ena_dev;

	if (hints->admin_completion_tx_timeout)
		ena_dev->admin_queue.completion_timeout =
		    hints->admin_completion_tx_timeout * 1000;

	if (hints->mmio_read_timeout)
		/* convert to usec */
		ena_dev->mmio_read.reg_read_to = hints->mmio_read_timeout * 1000;

	if (hints->missed_tx_completion_count_threshold_to_reset)
		adapter->missing_tx_threshold =
		    hints->missed_tx_completion_count_threshold_to_reset;

	if (hints->missing_tx_completion_timeout) {
		if (hints->missing_tx_completion_timeout ==
		    ENA_HW_HINTS_NO_TIMEOUT)
			adapter->missing_tx_timeout = ENA_HW_HINTS_NO_TIMEOUT;
		else
			adapter->missing_tx_timeout = NANOSECONDS_IN_MSEC *
			    hints->missing_tx_completion_timeout; //In ms
	}

	if (hints->driver_watchdog_timeout) {
		if (hints->driver_watchdog_timeout == ENA_HW_HINTS_NO_TIMEOUT)
			adapter->keep_alive_timeout = ENA_HW_HINTS_NO_TIMEOUT;
		else
			adapter->keep_alive_timeout = NANOSECONDS_IN_MSEC *
			    hints->driver_watchdog_timeout; //In ms
	}
}

static void
ena_timer_service(void *data)
{
	struct ena_adapter *adapter = (struct ena_adapter *)data;

	check_for_missing_keep_alive(adapter);

	check_for_admin_com_state(adapter);

	check_for_missing_completions(adapter);

	check_for_empty_rx_ring(adapter);

	if (unlikely(ENA_FLAG_ISSET(ENA_FLAG_TRIGGER_RESET, adapter))) {
		/*
		 * Timeout when validating version indicates that the device
		 * became unresponsive. If that happens skip the reset and
		 * reschedule timer service, so the reset can be retried later.
		 */
		if (ena_com_validate_version(adapter->ena_dev) ==
		    ENA_COM_TIMER_EXPIRED) {
			ena_log(adapter->pdev, WARN,
			    "FW unresponsive, skipping reset");
			ENA_TIMER_RESET(adapter);
			return;
		}
		ena_log(adapter->pdev, WARN, "Trigger reset is on");
		taskqueue_enqueue(adapter->reset_tq, &adapter->reset_task);
		return;
	}

	/*
	 * Schedule another timeout one second from now.
	 */
	ENA_TIMER_RESET(adapter);
}

void
ena_destroy_device(struct ena_adapter *adapter, bool graceful)
{
	struct ena_com_dev *ena_dev = adapter->ena_dev;
	bool dev_up;

	if (!ENA_FLAG_ISSET(ENA_FLAG_DEVICE_RUNNING, adapter))
		return;

	if (!graceful)
		adapter->ifp->if_link_state = LINK_STATE_DOWN;

	ENA_TIMER_DRAIN(adapter);

	dev_up = ENA_FLAG_ISSET(ENA_FLAG_DEV_UP, adapter);
	if (dev_up)
		ENA_FLAG_SET_ATOMIC(ENA_FLAG_DEV_UP_BEFORE_RESET, adapter);

	if (!graceful)
		ena_com_set_admin_running_state(ena_dev, false);

	if (ENA_FLAG_ISSET(ENA_FLAG_DEV_UP, adapter))
		ena_down(adapter);

	/*
	 * Stop the device from sending AENQ events (if the device was up, and
	 * the trigger reset was on, ena_down already performs device reset)
	 */
	if (!(ENA_FLAG_ISSET(ENA_FLAG_TRIGGER_RESET, adapter) && dev_up))
		ena_com_dev_reset(adapter->ena_dev, adapter->reset_reason);

	ena_free_mgmnt_irq(adapter);

	ena_disable_msix(adapter);

	/*
	 * IO rings resources should be freed because `ena_restore_device()`
	 * calls (not directly) `ena_enable_msix()`, which re-allocates MSIX
	 * vectors. The amount of MSIX vectors after destroy-restore may be
	 * different than before. Therefore, IO rings resources should be
	 * established from scratch each time.
	 */
	ena_free_all_io_rings_resources(adapter);

	ena_com_abort_admin_commands(ena_dev);

	ena_com_wait_for_abort_completion(ena_dev);

	ena_com_admin_destroy(ena_dev);

	ena_com_mmio_reg_read_request_destroy(ena_dev);

	adapter->reset_reason = ENA_REGS_RESET_NORMAL;

	ENA_FLAG_CLEAR_ATOMIC(ENA_FLAG_TRIGGER_RESET, adapter);
	ENA_FLAG_CLEAR_ATOMIC(ENA_FLAG_DEVICE_RUNNING, adapter);
}

static int
ena_device_validate_params(struct ena_adapter *adapter,
    struct ena_com_dev_get_features_ctx *get_feat_ctx)
{
	if (memcmp(get_feat_ctx->dev_attr.mac_addr, adapter->mac_addr,
	    ETHER_ADDR_LEN) != 0) {
		ena_log(adapter->pdev, ERR, "Error, mac addresses differ");
		return (EINVAL);
	}

	if (get_feat_ctx->dev_attr.max_mtu < adapter->ifp->if_mtu) {
		ena_log(adapter->pdev, ERR,
		    "Error, device max mtu is smaller than ifp MTU");
		return (EINVAL);
	}

	return 0;
}

int
ena_restore_device(struct ena_adapter *adapter)
{
	struct ena_com_dev_get_features_ctx get_feat_ctx;
	struct ena_com_dev *ena_dev = adapter->ena_dev;
	pci::device *dev = adapter->pdev;
	int wd_active;
	int rc;

	ENA_FLAG_SET_ATOMIC(ENA_FLAG_ONGOING_RESET, adapter);

	rc = ena_device_init(adapter, dev, &get_feat_ctx, &wd_active);
	if (rc != 0) {
		ena_log(dev, ERR, "Cannot initialize device");
		goto err;
	}
	/*
	 * Only enable WD if it was enabled before reset, so it won't override
	 * value set by the user by the sysctl.
	 */
	if (adapter->wd_active != 0)
		adapter->wd_active = wd_active;

	rc = ena_device_validate_params(adapter, &get_feat_ctx);
	if (rc != 0) {
		ena_log(dev, ERR, "Validation of device parameters failed");
		goto err_device_destroy;
	}

	ENA_FLAG_CLEAR_ATOMIC(ENA_FLAG_ONGOING_RESET, adapter);
	/* Make sure we don't have a race with AENQ Links state handler */
	if (ENA_FLAG_ISSET(ENA_FLAG_LINK_UP, adapter))
		adapter->ifp->if_link_state = LINK_STATE_UP;

	rc = ena_enable_msix_and_set_admin_interrupts(adapter);
	if (rc != 0) {
		ena_log(dev, ERR, "Enable MSI-X failed");
		goto err_device_destroy;
	}

	/*
	 * Effective value of used MSIX vectors should be the same as before
	 * `ena_destroy_device()`, if possible, or closest to it if less vectors
	 * are available.
	 */
	if ((adapter->msix_vecs - ENA_ADMIN_MSIX_VEC) < adapter->num_io_queues)
		adapter->num_io_queues = adapter->msix_vecs - ENA_ADMIN_MSIX_VEC;

	/* Re-initialize rings basic information */
	ena_init_io_rings(adapter);

	/* If the interface was up before the reset bring it up */
	if (ENA_FLAG_ISSET(ENA_FLAG_DEV_UP_BEFORE_RESET, adapter)) {
		rc = ena_up(adapter);
		if (rc != 0) {
			ena_log(dev, ERR, "Failed to create I/O queues");
			goto err_disable_msix;
		}
	}

	/* Indicate that device is running again and ready to work */
	ENA_FLAG_SET_ATOMIC(ENA_FLAG_DEVICE_RUNNING, adapter);

	/*
	 * As the AENQ handlers weren't executed during reset because
	 * the flag ENA_FLAG_DEVICE_RUNNING was turned off, the
	 * timestamp must be updated again That will prevent next reset
	 * caused by missing keep alive.
	 */
	adapter->keep_alive_timestamp = osv::clock::uptime::now().time_since_epoch().count();
	ENA_TIMER_RESET(adapter);

	ENA_FLAG_CLEAR_ATOMIC(ENA_FLAG_DEV_UP_BEFORE_RESET, adapter);

	return (rc);

err_disable_msix:
	ena_free_mgmnt_irq(adapter);
	ena_disable_msix(adapter);
err_device_destroy:
	ena_com_abort_admin_commands(ena_dev);
	ena_com_wait_for_abort_completion(ena_dev);
	ena_com_admin_destroy(ena_dev);
	ena_com_dev_reset(ena_dev, ENA_REGS_RESET_DRIVER_INVALID_STATE);
	ena_com_mmio_reg_read_request_destroy(ena_dev);
err:
	ENA_FLAG_CLEAR_ATOMIC(ENA_FLAG_DEVICE_RUNNING, adapter);
	ENA_FLAG_CLEAR_ATOMIC(ENA_FLAG_ONGOING_RESET, adapter);
	ena_log(dev, ERR, "Reset attempt failed. Can not reset the device");

	return (rc);
}

static void
ena_reset_task(void *arg, int pending)
{
	struct ena_adapter *adapter = (struct ena_adapter *)arg;

	ENA_LOCK_LOCK();
	if (likely(ENA_FLAG_ISSET(ENA_FLAG_TRIGGER_RESET, adapter))) {
		ena_destroy_device(adapter, false);
		ena_restore_device(adapter);

		ena_log(adapter->pdev, INFO,
		    "Device reset completed successfully, Driver info: %s",
		    ena_version);
	}
	ENA_LOCK_UNLOCK();
}

static void
ena_free_stats(struct ena_adapter *adapter)
{
	ena_free_counters((counter_u64_t *)&adapter->hw_stats,
	    sizeof(struct ena_hw_stats));
	ena_free_counters((counter_u64_t *)&adapter->dev_stats,
	    sizeof(struct ena_stats_dev));

}

static void
free_adapter(ena_adapter *adapter)
{
	if (adapter) {
		adapter->~ena_adapter();
		free(adapter);
	}
}

/**
 * ena_attach - Device Initialization Routine
 * @pdev: device information struct
 *
 * Returns 0 on success, otherwise on failure.
 *
 * ena_attach initializes an adapter identified by a device structure.
 * The OS initialization, configuring of the adapter private structure,
 * and a hardware reset occur.
 **/
int
ena_attach(pci::device* pdev, ena_adapter **_adapter)
{
	struct ena_adapter *adapter;
	struct ena_com_dev_get_features_ctx get_feat_ctx;
	struct ena_calc_queue_size_ctx calc_queue_ctx = { 0 };
	static int version_printed;
	struct ena_com_dev *ena_dev = NULL;
	uint32_t max_num_io_queues;
	int rc;

	adapter = aligned_new<ena_adapter>();
	adapter->pdev = pdev;
	adapter->first_bind = -1;

	/*
	 * Set up the timer service - driver is responsible for avoiding
	 * concurrency, as the callout won't be using any locking inside.
	 */
	ENA_TIMER_INIT(adapter);
	adapter->keep_alive_timeout = ENA_DEFAULT_KEEP_ALIVE_TO;
	adapter->missing_tx_timeout = ENA_DEFAULT_TX_CMP_TO;
	adapter->missing_tx_max_queues = ENA_DEFAULT_TX_MONITORED_QUEUES;
	adapter->missing_tx_threshold = ENA_DEFAULT_TX_CMP_THRESHOLD;

	if (version_printed++ == 0)
		ena_log(pdev, INFO, "%s", ena_version);

	/* Allocate memory for ena_dev structure */
	ena_dev = static_cast<ena_com_dev*>(malloc(sizeof(struct ena_com_dev), M_DEVBUF,
	    M_WAITOK | M_ZERO));

	adapter->ena_dev = ena_dev;
	ena_dev->dmadev = pdev;

	adapter->registers = pdev->get_bar(ENA_REG_BAR + 1);
	if (unlikely(adapter->registers == NULL)) {
		ena_log(pdev, ERR,
		    "unable to allocate bus resource: registers!");
		rc = ENOMEM;
		goto err_dev_free;
	}
	adapter->registers->map();

	ena_dev->bus = malloc(sizeof(struct ena_bus), M_DEVBUF,
	    M_WAITOK | M_ZERO);

	/* Store register resources */
	((struct ena_bus *)(ena_dev->bus))->reg_bar = adapter->registers;

	if (unlikely(((struct ena_bus *)(ena_dev->bus))->reg_bar == 0)) {
		ena_log(pdev, ERR, "failed to pmap registers bar");
		rc = ENXIO;
		goto err_bus_free;
	}

	/* Initially clear all the flags */
	ENA_FLAG_ZERO(adapter);

	/* Device initialization */
	rc = ena_device_init(adapter, pdev, &get_feat_ctx, &adapter->wd_active);
	if (unlikely(rc != 0)) {
		ena_log(pdev, ERR, "ENA device init failed! (err: %d)", rc);
		rc = ENXIO;
		goto err_bus_free;
	}

	adapter->keep_alive_timestamp = osv::clock::uptime::now().time_since_epoch().count();

	adapter->tx_offload_cap = get_feat_ctx.offload.tx;

	memcpy(adapter->mac_addr, get_feat_ctx.dev_attr.mac_addr, ETHER_ADDR_LEN);

	calc_queue_ctx.pdev = pdev;
	calc_queue_ctx.ena_dev = ena_dev;
	calc_queue_ctx.get_feat_ctx = &get_feat_ctx;

	/* Calculate initial and maximum IO queue number and size */
	max_num_io_queues = ena_calc_max_io_queue_num(pdev, ena_dev,
	    &get_feat_ctx);
	rc = ena_calc_io_queue_size(&calc_queue_ctx);
	if (unlikely((rc != 0) || (max_num_io_queues <= 0))) {
		rc = EFAULT;
		goto err_com_free;
	}

	adapter->requested_tx_ring_size = calc_queue_ctx.tx_queue_size;
	adapter->requested_rx_ring_size = calc_queue_ctx.rx_queue_size;
	adapter->max_tx_ring_size = calc_queue_ctx.max_tx_queue_size;
	adapter->max_rx_ring_size = calc_queue_ctx.max_rx_queue_size;
	adapter->max_tx_sgl_size = calc_queue_ctx.max_tx_sgl_size;
	adapter->max_rx_sgl_size = calc_queue_ctx.max_rx_sgl_size;

	adapter->max_num_io_queues = max_num_io_queues;
	ena_log(pdev, INFO, "ena_attach: set max_num_io_queues to %d", max_num_io_queues);

	adapter->buf_ring_size = ENA_DEFAULT_BUF_RING_SIZE;

	adapter->max_mtu = get_feat_ctx.dev_attr.max_mtu;

	adapter->reset_reason = ENA_REGS_RESET_NORMAL;

	/*
	 * The amount of requested MSIX vectors is equal to
	 * adapter::max_num_io_queues (see `ena_enable_msix()`), plus a constant
	 * number of admin queue interrupts. The former is initially determined
	 * by HW capabilities (see `ena_calc_max_io_queue_num())` but may not be
	 * achieved if there are not enough system resources. By default, the
	 * number of effectively used IO queues is the same but later on it can
	 * be limited by the user using sysctl interface.
	 */
	rc = ena_enable_msix_and_set_admin_interrupts(adapter);
	if (unlikely(rc != 0)) {
		ena_log(pdev, ERR,
		    "Failed to enable and set the admin interrupts");
		goto err_io_free;
	}
	/* By default all of allocated MSIX vectors are actively used */
	adapter->num_io_queues = adapter->msix_vecs - ENA_ADMIN_MSIX_VEC;

	/* initialize rings basic information */
	ena_init_io_rings(adapter);

	/* Initialize statistics */
	ena_alloc_counters((counter_u64_t *)&adapter->dev_stats,
	    sizeof(struct ena_stats_dev));
	ena_alloc_counters((counter_u64_t *)&adapter->hw_stats,
	    sizeof(struct ena_hw_stats));

	/* setup network interface */
	rc = ena_setup_ifnet(pdev, adapter, &get_feat_ctx);
	if (unlikely(rc != 0)) {
		ena_log(pdev, ERR, "Error with network interface setup");
		goto err_msix_free;
	}

	/* Initialize reset task queue */
	TASK_INIT(&adapter->reset_task, 0, ena_reset_task, adapter);
	adapter->reset_tq = taskqueue_create("ena_reset_enqueue",
	    M_WAITOK | M_ZERO, taskqueue_thread_enqueue, &adapter->reset_tq);
	taskqueue_start_threads(&adapter->reset_tq, 1, PI_NET, "ena rstq" );

	/* Tell the stack that the interface is not active */
	adapter->ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
	adapter->ifp->if_drv_flags |= IFF_DRV_OACTIVE;
	ENA_FLAG_SET_ATOMIC(ENA_FLAG_DEVICE_RUNNING, adapter);

	/* Run the timer service */
	ENA_TIMER_RESET(adapter);

	*_adapter = adapter;
	return (0);

err_msix_free:
	ena_free_stats(adapter);
	ena_com_dev_reset(adapter->ena_dev, ENA_REGS_RESET_INIT_ERR);
	ena_free_mgmnt_irq(adapter);
	ena_disable_msix(adapter);
err_io_free:
	ena_free_all_io_rings_resources(adapter);
err_com_free:
	ena_com_admin_destroy(ena_dev);
	ena_com_delete_host_info(ena_dev);
	ena_com_mmio_reg_read_request_destroy(ena_dev);
err_bus_free:
	free(ena_dev->bus, M_DEVBUF);
	ena_free_pci_resources(adapter);
err_dev_free:
	free(ena_dev, M_DEVBUF);

	free_adapter(adapter);
	return (rc);
}

/**
 * ena_detach - Device Removal Routine
 * @pdev: device information struct
 *
 * ena_detach is called by the device subsystem to alert the driver
 * that it should release a PCI device.
 **/
int
ena_detach(ena_adapter *adapter)
{
	struct ena_com_dev *ena_dev = adapter->ena_dev;

	ether_ifdetach(adapter->ifp);

	/* Stop timer service */
	ENA_LOCK_LOCK();
	ENA_TIMER_DRAIN(adapter);
	ENA_LOCK_UNLOCK();

	/* Release reset task */
	while (taskqueue_cancel(adapter->reset_tq, &adapter->reset_task, NULL))
		taskqueue_drain(adapter->reset_tq, &adapter->reset_task);
	taskqueue_free(adapter->reset_tq);

	ENA_LOCK_LOCK();
	ena_down(adapter);
	ena_destroy_device(adapter, true);
	ENA_LOCK_UNLOCK();

	ena_free_stats(adapter);

	ena_free_irqs(adapter);

	ena_free_pci_resources(adapter);

	if (adapter->rss_indir != NULL)
		free(adapter->rss_indir, M_DEVBUF);

	if (likely(ENA_FLAG_ISSET(ENA_FLAG_RSS_ACTIVE, adapter)))
		ena_com_rss_destroy(ena_dev);

	ena_com_delete_host_info(ena_dev);

	if_free(adapter->ifp);

	free(ena_dev->bus, M_DEVBUF);

	free(ena_dev, M_DEVBUF);

	free_adapter(adapter);
	return 0;
}

/******************************************************************************
 ******************************** AENQ Handlers *******************************
 *****************************************************************************/
/**
 * ena_update_on_link_change:
 * Notify the network interface about the change in link status
 **/
static void
ena_update_on_link_change(void *adapter_data,
    struct ena_admin_aenq_entry *aenq_e)
{
	struct ena_adapter *adapter = (struct ena_adapter *)adapter_data;
	struct ena_admin_aenq_link_change_desc *aenq_desc;
	int status;
	if_t ifp;

	aenq_desc = (struct ena_admin_aenq_link_change_desc *)aenq_e;
	ifp = adapter->ifp;
	status = aenq_desc->flags &
	    ENA_ADMIN_AENQ_LINK_CHANGE_DESC_LINK_STATUS_MASK;

	if (status != 0) {
		ena_log(adapter->pdev, INFO, "link is UP");
		ENA_FLAG_SET_ATOMIC(ENA_FLAG_LINK_UP, adapter);
		if (!ENA_FLAG_ISSET(ENA_FLAG_ONGOING_RESET, adapter))
			ifp->if_link_state = LINK_STATE_UP;
	} else {
		ena_log(adapter->pdev, INFO, "link is DOWN");
		ifp->if_link_state = LINK_STATE_DOWN;
		ENA_FLAG_CLEAR_ATOMIC(ENA_FLAG_LINK_UP, adapter);
	}
}

static void
ena_notification(void *adapter_data, struct ena_admin_aenq_entry *aenq_e)
{
	struct ena_adapter *adapter = (struct ena_adapter *)adapter_data;
	struct ena_admin_ena_hw_hints *hints;

	ENA_WARN(aenq_e->aenq_common_desc.group != ENA_ADMIN_NOTIFICATION,
	    adapter->ena_dev, "Invalid group(%x) expected %x",
	    aenq_e->aenq_common_desc.group, ENA_ADMIN_NOTIFICATION);

	switch (aenq_e->aenq_common_desc.syndrome) {
	case ENA_ADMIN_UPDATE_HINTS:
		hints =
		    (struct ena_admin_ena_hw_hints *)(&aenq_e->inline_data_w4);
		ena_update_hints(adapter, hints);
		break;
	default:
		ena_log(adapter->pdev, ERR,
		    "Invalid aenq notification link state %d",
		    aenq_e->aenq_common_desc.syndrome);
	}
}

/**
 * This handler will called for unknown event group or unimplemented handlers
 **/
static void
unimplemented_aenq_handler(void *adapter_data,
    struct ena_admin_aenq_entry *aenq_e)
{
	ena_log(adapter->pdev, ERR,
	    "Unknown event was received or event with unimplemented handler");
}

/*
 * Contains pointers to event handlers, e.g. link state chage.
 */
static struct ena_aenq_handlers aenq_handlers = {
    handlers : {
	    [ENA_ADMIN_LINK_CHANGE] = ena_update_on_link_change,
	    [ENA_ADMIN_FATAL_ERROR] = nullptr,
	    [ENA_ADMIN_WARNING] = nullptr,
	    [ENA_ADMIN_NOTIFICATION] = ena_notification,
	    [ENA_ADMIN_KEEP_ALIVE] = ena_keep_alive_wd,
    },
    unimplemented_handler : unimplemented_aenq_handler
};

static int
ena_device_init(struct ena_adapter *adapter, pci::device *pdev,
    struct ena_com_dev_get_features_ctx *get_feat_ctx, int *wd_active)
{
	struct ena_com_dev *ena_dev = adapter->ena_dev;
	bool readless_supported;
	uint32_t aenq_groups;
	int rc;

	rc = ena_com_mmio_reg_read_request_init(ena_dev);
	if (unlikely(rc != 0)) {
		ena_log(pdev, ERR, "failed to init mmio read less");
		return (rc);
	}

	/*
	 * The PCIe configuration space revision id indicate if mmio reg
	 * read is disabled
	 */
	readless_supported = !(pdev->get_revision_id() & ENA_MMIO_DISABLE_REG_READ);
	ena_com_set_mmio_read_mode(ena_dev, readless_supported);

	rc = ena_com_dev_reset(ena_dev, ENA_REGS_RESET_NORMAL);
	if (unlikely(rc != 0)) {
		ena_log(pdev, ERR, "Can not reset device");
		goto err_mmio_read_less;
	}

	rc = ena_com_validate_version(ena_dev);
	if (unlikely(rc != 0)) {
		ena_log(pdev, ERR, "device version is too low");
		goto err_mmio_read_less;
	}

	/* ENA admin level init */
	rc = ena_com_admin_init(ena_dev, &aenq_handlers);
	if (unlikely(rc != 0)) {
		ena_log(pdev, ERR,
		    "Can not initialize ena admin queue with device");
		goto err_mmio_read_less;
	}

	/*
	 * To enable the msix interrupts the driver needs to know the number
	 * of queues. So the driver uses polling mode to retrieve this
	 * information
	 */
	ena_com_set_admin_polling_mode(ena_dev, true);

	ena_config_host_info(ena_dev, pdev);

	/* Get Device Attributes */
	rc = ena_com_get_dev_attr_feat(ena_dev, get_feat_ctx);
	if (unlikely(rc != 0)) {
		ena_log(pdev, ERR,
		    "Cannot get attribute for ena device rc: %d", rc);
		goto err_admin_init;
	}

	aenq_groups = BIT(ENA_ADMIN_LINK_CHANGE) |
	    BIT(ENA_ADMIN_FATAL_ERROR) |
	    BIT(ENA_ADMIN_WARNING) |
	    BIT(ENA_ADMIN_NOTIFICATION) |
	    BIT(ENA_ADMIN_KEEP_ALIVE);

	aenq_groups &= get_feat_ctx->aenq.supported_groups;
	rc = ena_com_set_aenq_config(ena_dev, aenq_groups);
	if (unlikely(rc != 0)) {
		ena_log(pdev, ERR, "Cannot configure aenq groups rc: %d", rc);
		goto err_admin_init;
	}

	*wd_active = !!(aenq_groups & BIT(ENA_ADMIN_KEEP_ALIVE));

	rc = ena_set_queues_placement_policy(pdev, ena_dev, &get_feat_ctx->llq, nullptr);
	if (unlikely(rc != 0)) {
		ena_log(pdev, ERR, "Failed to set placement policy");
		goto err_admin_init;
	}

	return (0);

err_admin_init:
	ena_com_delete_host_info(ena_dev);
	ena_com_admin_destroy(ena_dev);
err_mmio_read_less:
	ena_com_mmio_reg_read_request_destroy(ena_dev);

	return (rc);
}

