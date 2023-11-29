/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2015-2020 Amazon.com, Inc. or its affiliates.
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

//#define ENA_LOG_ENABLE 1
//#define ENA_LOG_IO_ENABLE 1

#include "ena.h"
#include "ena_datapath.h"

#include <osv/sched.hh>

static inline void critical_enter()  { sched::preempt_disable(); }
static inline void critical_exit() { sched::preempt_enable(); }

#include <sys/buf_ring.h>

//#include <netinet6/ip6_var.h>

/*********************************************************************
 *  Static functions prototypes
 *********************************************************************/

static int ena_tx_cleanup(struct ena_ring *);
static int ena_rx_cleanup(struct ena_ring *);
static inline int ena_get_tx_req_id(struct ena_ring *tx_ring,
    struct ena_com_io_cq *io_cq, uint16_t *req_id);
static struct mbuf *ena_rx_mbuf(struct ena_ring *, struct ena_com_rx_buf_info *,
    struct ena_com_rx_ctx *, uint16_t *);
static inline void ena_rx_checksum(struct ena_ring *, struct ena_com_rx_ctx *,
    struct mbuf *);
static void ena_tx_csum(struct ena_com_tx_ctx *, struct mbuf *, bool);
static int ena_check_and_collapse_mbuf(struct ena_ring *tx_ring,
    struct mbuf **mbuf);
static int ena_xmit_mbuf(struct ena_ring *, struct mbuf **);
static void ena_start_xmit(struct ena_ring *);

/*********************************************************************
 *  Global functions
 *********************************************************************/

void
ena_cleanup(struct ena_que *que)
{
	struct ena_adapter *adapter = que->adapter;
	if_t ifp = adapter->ifp;
	struct ena_ring *tx_ring;
	struct ena_ring *rx_ring;
	struct ena_com_io_cq *io_cq;
	struct ena_eth_io_intr_reg intr_reg;
	int qid, ena_qid;
	int txc, rxc, i;

	if (unlikely((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0))
		return;

	ena_log_io(adapter->pdev, INFO, "MSI-X TX/RX routine");

	tx_ring = que->tx_ring;
	rx_ring = que->rx_ring;
	qid = que->id;
	ena_qid = ENA_IO_TXQ_IDX(qid);
	io_cq = &adapter->ena_dev->io_cq_queues[ena_qid];

	tx_ring->first_interrupt.store(1);
	rx_ring->first_interrupt.store(1);

	for (i = 0; i < ENA_CLEAN_BUDGET; ++i) {
		rxc = ena_rx_cleanup(rx_ring);
		txc = ena_tx_cleanup(tx_ring);

		if (unlikely((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0))
			return;

		if ((txc != ENA_TX_BUDGET) && (rxc != ENA_RX_BUDGET))
			break;
	}

	/* Signal that work is done and unmask interrupt */
	ena_com_update_intr_reg(&intr_reg, ENA_RX_IRQ_INTERVAL,
	    ENA_TX_IRQ_INTERVAL, true);
	counter_u64_add(tx_ring->tx_stats.unmask_interrupt_num, 1);
	ena_com_unmask_intr(io_cq, &intr_reg);
}

void
ena_deferred_mq_start(struct ena_ring *tx_ring )
{
	if_t ifp = tx_ring->adapter->ifp;

	while (!buf_ring_empty(tx_ring->br) && tx_ring->running &&
	    (ifp->if_drv_flags & IFF_DRV_RUNNING) != 0) {
		ENA_RING_MTX_LOCK(tx_ring);
		ena_start_xmit(tx_ring);
		ENA_RING_MTX_UNLOCK(tx_ring);
	}
}

int
ena_mq_start(if_t ifp, struct mbuf *m)
{
	struct ena_adapter *adapter = static_cast<ena_adapter *>(ifp->if_softc);
	struct ena_ring *tx_ring;
	int ret, is_br_empty;
	uint32_t i;

	if (unlikely((adapter->ifp->if_drv_flags & IFF_DRV_RUNNING) == 0))
		return (ENODEV);

	/* Which queue to use */
	/*
	 * If everything is setup correctly, it should be the
	 * same bucket that the current CPU we're on is.
	 * It should improve performance.
	 */
	if (M_HASHTYPE_GET(m) != M_HASHTYPE_NONE) {
		i = m->M_dat.MH.MH_pkthdr.flowid % adapter->num_io_queues;
	} else {
		i = sched::cpu::current()->id % adapter->num_io_queues;
	}
	tx_ring = &adapter->tx_ring[i];

	/* Check if br is empty before putting packet */
	is_br_empty = buf_ring_empty(tx_ring->br);
	ret = buf_ring_enqueue(tx_ring->br, m);
	if (unlikely(ret != 0)) {
		m_freem(m);
		tx_ring->enqueue_thread->wake_with([tx_ring] { tx_ring->enqueue_pending++; });
		return (ret);
	}

	if (is_br_empty && (ENA_RING_MTX_TRYLOCK(tx_ring) != 0)) {
		ena_start_xmit(tx_ring);
		ENA_RING_MTX_UNLOCK(tx_ring);
	} else {
		tx_ring->enqueue_thread->wake_with([tx_ring] { tx_ring->enqueue_pending++; });
	}

	return (0);
}

void
ena_qflush(if_t ifp)
{
	struct ena_adapter *adapter = static_cast<ena_adapter *>(ifp->if_softc);
	struct ena_ring *tx_ring = adapter->tx_ring;
	int i;

	for (i = 0; i < adapter->num_io_queues; ++i, ++tx_ring)
		if (!buf_ring_empty(tx_ring->br)) {
			ENA_RING_MTX_LOCK(tx_ring);
			struct mbuf *m;
			while ((m = (struct mbuf *)buf_ring_dequeue_sc(tx_ring->br)) != NULL)
				m_freem(m);
			ENA_RING_MTX_UNLOCK(tx_ring);
		}

	if_qflush(ifp);
}

/*********************************************************************
 *  Static functions
 *********************************************************************/

static inline int
ena_get_tx_req_id(struct ena_ring *tx_ring, struct ena_com_io_cq *io_cq,
    uint16_t *req_id)
{
	struct ena_adapter *adapter = tx_ring->adapter;
	int rc;

	rc = ena_com_tx_comp_req_id_get(io_cq, req_id);
	if (rc == ENA_COM_TRY_AGAIN)
		return (EAGAIN);

	if (unlikely(rc != 0)) {
		ena_log(adapter->pdev, ERR, "Invalid req_id %hu in qid %hu",
		    *req_id, tx_ring->qid);
		counter_u64_add(tx_ring->tx_stats.bad_req_id, 1);
		goto err;
	}

	if (tx_ring->tx_buffer_info[*req_id].mbuf != NULL)
		return (0);

	ena_log(adapter->pdev, ERR,
	    "tx_info doesn't have valid mbuf. req_id %hu qid %hu",
	    *req_id, tx_ring->qid);
err:
	ena_trigger_reset(adapter, ENA_REGS_RESET_INV_TX_REQ_ID);

	return (EFAULT);
}

/**
 * ena_tx_cleanup - clear sent packets and corresponding descriptors
 * @tx_ring: ring for which we want to clean packets
 *
 * Once packets are sent, we ask the device in a loop for no longer used
 * descriptors. We find the related mbuf chain in a map (index in an array)
 * and free it, then update ring state.
 * This is performed in "endless" loop, updating ring pointers every
 * TX_COMMIT. The first check of free descriptor is performed before the actual
 * loop, then repeated at the loop end.
 **/
static int
ena_tx_cleanup(struct ena_ring *tx_ring)
{
	struct ena_adapter *adapter;
	struct ena_com_io_cq *io_cq;
	uint16_t next_to_clean;
	uint16_t req_id;
	uint16_t ena_qid;
	unsigned int total_done = 0;
	int rc;
	int commit = ENA_TX_COMMIT;
	int budget = ENA_TX_BUDGET;
	int work_done;
	bool above_thresh;

	adapter = tx_ring->que->adapter;
	ena_qid = ENA_IO_TXQ_IDX(tx_ring->que->id);
	io_cq = &adapter->ena_dev->io_cq_queues[ena_qid];
	next_to_clean = tx_ring->next_to_clean;

	do {
		struct ena_tx_buffer *tx_info;
		struct mbuf *mbuf;

		rc = ena_get_tx_req_id(tx_ring, io_cq, &req_id);
		if (unlikely(rc != 0))
			break;

		tx_info = &tx_ring->tx_buffer_info[req_id];

		mbuf = tx_info->mbuf;

		tx_info->mbuf = NULL;
		tx_info->timestamp = 0;

		ena_log_io(adapter->pdev, DBG, "tx: q %d mbuf %p completed",
		    tx_ring->qid, mbuf);

		m_freem(mbuf);

		total_done += tx_info->tx_descs;

		tx_ring->free_tx_ids[next_to_clean] = req_id;
		next_to_clean = ENA_TX_RING_IDX_NEXT(next_to_clean,
		    tx_ring->ring_size);

		if (unlikely(--commit == 0)) {
			commit = ENA_TX_COMMIT;
			/* update ring state every ENA_TX_COMMIT descriptor */
			tx_ring->next_to_clean = next_to_clean;
			ena_com_comp_ack(
			    &adapter->ena_dev->io_sq_queues[ena_qid],
			    total_done);
			ena_com_update_dev_comp_head(io_cq);
			total_done = 0;
		}
	} while (likely(--budget));

	work_done = ENA_TX_BUDGET - budget;

	ena_log_io(adapter->pdev, DBG, "tx: q %d done. total pkts: %d",
	    tx_ring->qid, work_done);

	/* If there is still something to commit update ring state */
	if (likely(commit != ENA_TX_COMMIT)) {
		tx_ring->next_to_clean = next_to_clean;
		ena_com_comp_ack(&adapter->ena_dev->io_sq_queues[ena_qid],
		    total_done);
		ena_com_update_dev_comp_head(io_cq);
	}

	/*
	 * Need to make the rings circular update visible to
	 * ena_xmit_mbuf() before checking for tx_ring->running.
	 */
	mb();

	above_thresh = ena_com_sq_have_enough_space(tx_ring->ena_com_io_sq,
	    ENA_TX_RESUME_THRESH);
	if (unlikely(!tx_ring->running && above_thresh)) {
		ENA_RING_MTX_LOCK(tx_ring);
		above_thresh = ena_com_sq_have_enough_space(
		    tx_ring->ena_com_io_sq, ENA_TX_RESUME_THRESH);
		if (!tx_ring->running && above_thresh) {
			tx_ring->running = true;
			counter_u64_add(tx_ring->tx_stats.queue_wakeup, 1);
			tx_ring->enqueue_thread->wake_with([tx_ring] { tx_ring->enqueue_pending++; });
		}
		ENA_RING_MTX_UNLOCK(tx_ring);
	}

	tx_ring->tx_last_cleanup_ticks = bsd_ticks;

	return (work_done);
}

/**
 * ena_rx_mbuf - assemble mbuf from descriptors
 * @rx_ring: ring for which we want to clean packets
 * @ena_bufs: buffer info
 * @ena_rx_ctx: metadata for this packet(s)
 * @next_to_clean: ring pointer, will be updated only upon success
 *
 **/
static struct mbuf *
ena_rx_mbuf(struct ena_ring *rx_ring, struct ena_com_rx_buf_info *ena_bufs,
    struct ena_com_rx_ctx *ena_rx_ctx, uint16_t *next_to_clean)
{
	struct mbuf *mbuf;
	struct ena_rx_buffer *rx_info;
	struct ena_adapter *adapter;
	unsigned int descs = ena_rx_ctx->descs;
	uint16_t ntc, len, req_id, buf = 0;

	ntc = *next_to_clean;
	adapter = rx_ring->adapter;

	len = ena_bufs[buf].len;
	req_id = ena_bufs[buf].req_id;
	rx_info = &rx_ring->rx_buffer_info[req_id];
	if (unlikely(rx_info->mbuf == NULL)) {
		ena_log(adapter->pdev, ERR, "NULL mbuf in rx_info");
		return (NULL);
	}

	ena_log_io(adapter->pdev, DBG, "rx_info %p, mbuf %p, paddr %jx", rx_info,
	    rx_info->mbuf, (uintmax_t)rx_info->ena_buf.paddr);

	mbuf = rx_info->mbuf;
	mbuf->m_hdr.mh_flags |= M_PKTHDR;
	mbuf->M_dat.MH.MH_pkthdr.len = len;
	mbuf->m_hdr.mh_len = len;
	/* Only for the first segment the data starts at specific offset */
	mbuf->m_hdr.mh_data = static_cast<caddr_t>(mtodo(mbuf, ena_rx_ctx->pkt_offset));
	ena_log_io(adapter->pdev, DBG, "Mbuf data offset=%u", ena_rx_ctx->pkt_offset);
	mbuf->M_dat.MH.MH_pkthdr.rcvif = rx_ring->que->adapter->ifp;

	ena_log_io(adapter->pdev, DBG, "rx mbuf 0x%p, flags=0x%x, len: %d", mbuf,
	    mbuf->m_hdr.mh_flags, mbuf->M_dat.MH.MH_pkthdr.len);

	rx_info->mbuf = NULL;
	rx_ring->free_rx_ids[ntc] = req_id;
	ntc = ENA_RX_RING_IDX_NEXT(ntc, rx_ring->ring_size);

	/*
	 * While we have more than 1 descriptors for one rcvd packet, append
	 * other mbufs to the main one
	 */
	while (--descs) {
		++buf;
		len = ena_bufs[buf].len;
		req_id = ena_bufs[buf].req_id;
		rx_info = &rx_ring->rx_buffer_info[req_id];

		if (unlikely(rx_info->mbuf == NULL)) {
			ena_log(adapter->pdev, ERR, "NULL mbuf in rx_info");
			/*
			 * If one of the required mbufs was not allocated yet,
			 * we can break there.
			 * All earlier used descriptors will be reallocated
			 * later and not used mbufs can be reused.
			 * The next_to_clean pointer will not be updated in case
			 * of an error, so caller should advance it manually
			 * in error handling routine to keep it up to date
			 * with hw ring.
			 */
			m_freem(mbuf);
			return (NULL);
		}

		if (unlikely(m_append(mbuf, len, rx_info->mbuf->m_hdr.mh_data) == 0)) {
			counter_u64_add(rx_ring->rx_stats.mbuf_alloc_fail, 1);
			ena_log_io(adapter->pdev, WARN, "Failed to append Rx mbuf %p",
			    mbuf);
		}

		ena_log_io(adapter->pdev, DBG, "rx mbuf updated. len %d",
		    mbuf->M_dat.MH.MH_pkthdr.len);

		m_freem(rx_info->mbuf);
		rx_info->mbuf = NULL;

		rx_ring->free_rx_ids[ntc] = req_id;
		ntc = ENA_RX_RING_IDX_NEXT(ntc, rx_ring->ring_size);
	}

	*next_to_clean = ntc;

	return (mbuf);
}

/**
 * ena_rx_checksum - indicate in mbuf if hw indicated a good cksum
 **/
static inline void
ena_rx_checksum(struct ena_ring *rx_ring, struct ena_com_rx_ctx *ena_rx_ctx,
    struct mbuf *mbuf)
{
	/* if IP and error */
	if (unlikely((ena_rx_ctx->l3_proto == ENA_ETH_IO_L3_PROTO_IPV4) &&
	    ena_rx_ctx->l3_csum_err)) {
		/* ipv4 checksum error */
		mbuf->M_dat.MH.MH_pkthdr.csum_flags = 0;
		counter_u64_add(rx_ring->rx_stats.csum_bad, 1);
		ena_log_io(pdev, DBG, "RX IPv4 header checksum error");
		return;
	}

	/* if TCP/UDP */
	if ((ena_rx_ctx->l4_proto == ENA_ETH_IO_L4_PROTO_TCP) ||
	    (ena_rx_ctx->l4_proto == ENA_ETH_IO_L4_PROTO_UDP)) {
		if (ena_rx_ctx->l4_csum_err) {
			/* TCP/UDP checksum error */
			mbuf->M_dat.MH.MH_pkthdr.csum_flags = 0;
			counter_u64_add(rx_ring->rx_stats.csum_bad, 1);
			ena_log_io(pdev, DBG, "RX L4 checksum error");
		} else {
			mbuf->M_dat.MH.MH_pkthdr.csum_flags= CSUM_IP_CHECKED;
			mbuf->M_dat.MH.MH_pkthdr.csum_flags |= CSUM_IP_VALID;
			counter_u64_add(rx_ring->rx_stats.csum_good, 1);
		}
	}
}

/**
 * ena_rx_cleanup - handle rx irq
 * @arg: ring for which irq is being handled
 **/
static int
ena_rx_cleanup(struct ena_ring *rx_ring)
{
	struct ena_adapter *adapter;
	struct mbuf *mbuf;
	struct ena_com_rx_ctx ena_rx_ctx;
	struct ena_com_io_cq *io_cq;
	struct ena_com_io_sq *io_sq;
	enum ena_regs_reset_reason_types reset_reason;
	if_t ifp;
	uint16_t ena_qid;
	uint16_t next_to_clean;
	uint32_t refill_required;
	uint32_t refill_threshold;
	uint32_t do_if_input = 0;
	unsigned int qid;
	int rc, i;
	int budget = ENA_RX_BUDGET;

	adapter = rx_ring->que->adapter;
	ifp = adapter->ifp;
	qid = rx_ring->que->id;
	ena_qid = ENA_IO_RXQ_IDX(qid);
	io_cq = &adapter->ena_dev->io_cq_queues[ena_qid];
	io_sq = &adapter->ena_dev->io_sq_queues[ena_qid];
	next_to_clean = rx_ring->next_to_clean;

	ena_log_io(adapter->pdev, DBG, "rx: qid %d", qid);

	do {
		ena_rx_ctx.ena_bufs = rx_ring->ena_bufs;
		ena_rx_ctx.max_bufs = adapter->max_rx_sgl_size;
		ena_rx_ctx.descs = 0;
		ena_rx_ctx.pkt_offset = 0;

		rc = ena_com_rx_pkt(io_cq, io_sq, &ena_rx_ctx);
		if (unlikely(rc != 0)) {
			if (rc == ENA_COM_NO_SPACE) {
				counter_u64_add(rx_ring->rx_stats.bad_desc_num,
				    1);
				reset_reason = ENA_REGS_RESET_TOO_MANY_RX_DESCS;
			} else {
				counter_u64_add(rx_ring->rx_stats.bad_req_id,
				    1);
				reset_reason = ENA_REGS_RESET_INV_RX_REQ_ID;
			}
			ena_trigger_reset(adapter, reset_reason);
			return (0);
		}

		if (unlikely(ena_rx_ctx.descs == 0))
			break;

		ena_log_io(adapter->pdev, DBG,
		    "rx: q %d got packet from ena. descs #: %d l3 proto %d l4 proto %d hash: %x",
		    rx_ring->qid, ena_rx_ctx.descs, ena_rx_ctx.l3_proto,
		    ena_rx_ctx.l4_proto, ena_rx_ctx.hash);

		/* Receive mbuf from the ring */
		mbuf = ena_rx_mbuf(rx_ring, rx_ring->ena_bufs, &ena_rx_ctx,
		    &next_to_clean);
		/* Exit if we failed to retrieve a buffer */
		if (unlikely(mbuf == NULL)) {
			for (i = 0; i < ena_rx_ctx.descs; ++i) {
				rx_ring->free_rx_ids[next_to_clean] =
				    rx_ring->ena_bufs[i].req_id;
				next_to_clean = ENA_RX_RING_IDX_NEXT(
				    next_to_clean, rx_ring->ring_size);
			}
			break;
		}

		if (((ifp->if_capenable & IFCAP_RXCSUM) != 0) ||
		    ((ifp->if_capenable & IFCAP_RXCSUM_IPV6) != 0)) {
			ena_rx_checksum(rx_ring, &ena_rx_ctx, mbuf);
		}

		counter_enter();
		counter_u64_add_protected(rx_ring->rx_stats.bytes,
		    mbuf->M_dat.MH.MH_pkthdr.len);
		counter_u64_add_protected(adapter->hw_stats.rx_bytes,
		    mbuf->M_dat.MH.MH_pkthdr.len);
		counter_exit();
		/*
		 * LRO is only for IP/TCP packets and TCP checksum of the packet
		 * should be computed by hardware.
		 */
		do_if_input = 1;
		if (((ifp->if_capenable & IFCAP_LRO) != 0)  &&
		    ((mbuf->M_dat.MH.MH_pkthdr.csum_flags & CSUM_IP_VALID) != 0) &&
		    (ena_rx_ctx.l4_proto == ENA_ETH_IO_L4_PROTO_TCP)) {
			/*
			 * Send to the stack if:
			 *  - LRO not enabled, or
			 *  - no LRO resources, or
			 *  - lro enqueue fails
			 */
			if ((rx_ring->lro.lro_cnt != 0) &&
			    (tcp_lro_rx(&rx_ring->lro, mbuf, 0) == 0))
				do_if_input = 0;
		}
		if (do_if_input != 0) {
			ena_log_io(adapter->pdev, DBG,
			    "calling if_input() with mbuf %p", mbuf);
			bool fast_path = ifp->if_classifier.post_packet(mbuf);
			if (!fast_path) {
				(*ifp->if_input)(ifp, mbuf);
			}
		}

		counter_enter();
		counter_u64_add_protected(rx_ring->rx_stats.cnt, 1);
		counter_u64_add_protected(adapter->hw_stats.rx_packets, 1);
		counter_exit();
	} while (--budget);

	rx_ring->next_to_clean = next_to_clean;

	refill_required = ena_com_free_q_entries(io_sq);
	refill_threshold = min_t(int,
	    rx_ring->ring_size / ENA_RX_REFILL_THRESH_DIVIDER,
	    ENA_RX_REFILL_THRESH_PACKET);

	if (refill_required > refill_threshold) {
		ena_com_update_dev_comp_head(rx_ring->ena_com_io_cq);
		ena_refill_rx_bufs(rx_ring, refill_required);
	}

        //TODO: Can wait? investigate https://github.com/freebsd/freebsd-src/commit/e936121d3140af047a498559493b9375a6ba6ba3
        //to port it back
	//tcp_lro_flush_all(&rx_ring->lro);

	return (ENA_RX_BUDGET - budget);
}

static void
ena_tx_csum(struct ena_com_tx_ctx *ena_tx_ctx, struct mbuf *mbuf,
    bool disable_meta_caching)
{
	struct ena_com_tx_meta *ena_meta;
	struct ether_vlan_header *eh;
	struct mbuf *mbuf_next;
	u32 mss;
	bool offload;
	uint16_t etype;
	int ehdrlen;
	struct ip *ip;
	int ipproto;
	int iphlen;
	struct tcphdr *th;
	int offset;

	offload = false;
	ena_meta = &ena_tx_ctx->ena_meta;
	mss = mbuf->M_dat.MH.MH_pkthdr.tso_segsz;

	if (mss != 0)
		offload = true;

	if ((mbuf->M_dat.MH.MH_pkthdr.csum_flags & CSUM_TSO) != 0)
		offload = true;

	if ((mbuf->M_dat.MH.MH_pkthdr.csum_flags & CSUM_OFFLOAD) != 0)
		offload = true;

	if ((mbuf->M_dat.MH.MH_pkthdr.csum_flags & CSUM6_OFFLOAD) != 0)
		offload = true;

	if (!offload) {
		if (disable_meta_caching) {
			memset(ena_meta, 0, sizeof(*ena_meta));
			ena_tx_ctx->meta_valid = 1;
		} else {
			ena_tx_ctx->meta_valid = 0;
		}
		return;
	}

	/* Determine where frame payload starts. */
	eh = mtod(mbuf, struct ether_vlan_header *);
	if (eh->evl_encap_proto == htons(ETHERTYPE_VLAN)) {
		etype = ntohs(eh->evl_proto);
		ehdrlen = ETHER_HDR_LEN + ETHER_VLAN_ENCAP_LEN;
	} else {
		etype = ntohs(eh->evl_encap_proto);
		ehdrlen = ETHER_HDR_LEN;
	}

	mbuf_next = m_getptr(mbuf, ehdrlen, &offset);

	switch (etype) {
	case ETHERTYPE_IP:
		ip = (struct ip *)(mtodo(mbuf_next, offset));
		iphlen = ip->ip_hl << 2;
		ipproto = ip->ip_p;
		ena_tx_ctx->l3_proto = ENA_ETH_IO_L3_PROTO_IPV4;
		if ((ip->ip_off & htons(IP_DF)) != 0)
			ena_tx_ctx->df = 1;
		break;
#ifdef INET6
	case ETHERTYPE_IPV6:
		ena_tx_ctx->l3_proto = ENA_ETH_IO_L3_PROTO_IPV6;
		iphlen = ip6_lasthdr(mbuf, ehdrlen, IPPROTO_IPV6, &ipproto);
		iphlen -= ehdrlen;
		ena_tx_ctx->df = 1;
		break;
#endif
	default:
		iphlen = 0;
		ipproto = 0;
		break;
	}

	mbuf_next = m_getptr(mbuf, iphlen + ehdrlen, &offset);
	th = (struct tcphdr *)(mtodo(mbuf_next, offset));

	if ((mbuf->M_dat.MH.MH_pkthdr.csum_flags & CSUM_IP) != 0) {
		ena_tx_ctx->l3_csum_enable = 1;
	}
	if ((mbuf->M_dat.MH.MH_pkthdr.csum_flags & CSUM_TSO) != 0) {
		ena_tx_ctx->tso_enable = 1;
		ena_meta->l4_hdr_len = (th->th_off);
	}

	if (ipproto == IPPROTO_TCP) {
		ena_tx_ctx->l4_proto = ENA_ETH_IO_L4_PROTO_TCP;
		if ((mbuf->M_dat.MH.MH_pkthdr.csum_flags &
		    (CSUM_TCP | CSUM_TCP_IPV6)) != 0)
			ena_tx_ctx->l4_csum_enable = 1;
		else
			ena_tx_ctx->l4_csum_enable = 0;
	} else if (ipproto == IPPROTO_UDP) {
		ena_tx_ctx->l4_proto = ENA_ETH_IO_L4_PROTO_UDP;
		if ((mbuf->M_dat.MH.MH_pkthdr.csum_flags &
		    (CSUM_UDP | CSUM_UDP_IPV6)) != 0)
			ena_tx_ctx->l4_csum_enable = 1;
		else
			ena_tx_ctx->l4_csum_enable = 0;
	} else {
		ena_tx_ctx->l4_proto = ENA_ETH_IO_L4_PROTO_UNKNOWN;
		ena_tx_ctx->l4_csum_enable = 0;
	}

	ena_meta->mss = mss;
	ena_meta->l3_hdr_len = iphlen;
	ena_meta->l3_hdr_offset = ehdrlen;
	ena_tx_ctx->meta_valid = 1;
}

static int
ena_check_and_collapse_mbuf(struct ena_ring *tx_ring, struct mbuf **mbuf)
{
	struct ena_adapter *adapter;
	struct mbuf *collapsed_mbuf;
	int num_frags;

	adapter = tx_ring->adapter;
	num_frags = ena_mbuf_count(*mbuf);

	/* One segment must be reserved for configuration descriptor. */
	if (num_frags < adapter->max_tx_sgl_size)
		return (0);

	if ((num_frags == adapter->max_tx_sgl_size) &&
	    ((*mbuf)->M_dat.MH.MH_pkthdr.len < tx_ring->tx_max_header_size))
		return (0);

	counter_u64_add(tx_ring->tx_stats.collapse, 1);

	collapsed_mbuf = m_collapse(*mbuf, M_NOWAIT,
	    adapter->max_tx_sgl_size - 1);
	if (unlikely(collapsed_mbuf == NULL)) {
		counter_u64_add(tx_ring->tx_stats.collapse_err, 1);
		return (ENOMEM);
	}

	/* If mbuf was collapsed succesfully, original mbuf is released. */
	*mbuf = collapsed_mbuf;

	return (0);
}

static int
ena_tx_map_mbuf(struct ena_ring *tx_ring, struct ena_tx_buffer *tx_info,
    struct mbuf *mbuf, void **push_hdr, u16 *header_len)
{
	int nsegs = 0;

	tx_info->mbuf = mbuf;

	for (struct mbuf *m = mbuf; m != NULL; m = m->m_hdr.mh_next) {
		int frag_len = m->m_hdr.mh_len;

		if (frag_len != 0) {
			struct ena_com_buf *ena_buf = &tx_info->bufs[nsegs];
			ena_buf->paddr = mmu::virt_to_phys(m->m_hdr.mh_data);
			ena_buf->len = frag_len;
			tx_info->num_of_bufs++;
			if (++nsegs >= ENA_PKT_MAX_BUFS) //TODO: Do we loose data if this is true?
				break;
		}
	}

	*push_hdr = NULL;
	/*
	 * header_len is just a hint for the device. Because FreeBSD is
	 * not giving us information about packet header length and it
	 * is not guaranteed that all packet headers will be in the 1st
	 * mbuf, setting header_len to 0 is making the device ignore
	 * this value and resolve header on it's own.
	 */
	*header_len = 0;

	return (0);
}

static int
ena_xmit_mbuf(struct ena_ring *tx_ring, struct mbuf **mbuf)
{
	struct ena_adapter *adapter;
	struct ena_tx_buffer *tx_info;
	struct ena_com_tx_ctx ena_tx_ctx;
	struct ena_com_dev *ena_dev;
	struct ena_com_io_sq *io_sq;
	void *push_hdr;
	uint16_t next_to_use;
	uint16_t req_id;
	uint16_t ena_qid;
	uint16_t header_len;
	int rc;
	int nb_hw_desc;

	ena_qid = ENA_IO_TXQ_IDX(tx_ring->que->id);
	adapter = tx_ring->que->adapter;
	ena_dev = adapter->ena_dev;
	io_sq = &ena_dev->io_sq_queues[ena_qid];

	rc = ena_check_and_collapse_mbuf(tx_ring, mbuf);
	if (unlikely(rc != 0)) {
		ena_log_io(adapter->pdev, WARN, "Failed to collapse mbuf! err: %d",
		    rc);
		return (rc);
	}

	ena_log_io(adapter->pdev, DBG, "Tx: %d bytes", (*mbuf)->M_dat.MH.MH_pkthdr.len);

	next_to_use = tx_ring->next_to_use;
	req_id = tx_ring->free_tx_ids[next_to_use];
	tx_info = &tx_ring->tx_buffer_info[req_id];
	tx_info->num_of_bufs = 0;

	ENA_WARN(tx_info->mbuf != NULL, adapter->ena_dev,
	    "mbuf isn't NULL for req_id %d", req_id);

	rc = ena_tx_map_mbuf(tx_ring, tx_info, *mbuf, &push_hdr, &header_len);
	if (unlikely(rc != 0)) {
		ena_log_io(adapter->pdev, WARN, "Failed to map TX mbuf");
		return (rc);
	}
	memset(&ena_tx_ctx, 0x0, sizeof(struct ena_com_tx_ctx));
	ena_tx_ctx.ena_bufs = tx_info->bufs;
	ena_tx_ctx.push_header = push_hdr;
	ena_tx_ctx.num_bufs = tx_info->num_of_bufs;
	ena_tx_ctx.req_id = req_id;
	ena_tx_ctx.header_len = header_len;

	/* Set flags and meta data */
	ena_tx_csum(&ena_tx_ctx, *mbuf, adapter->disable_meta_caching);

	if (tx_ring->acum_pkts == ENA_DB_THRESHOLD ||
	    ena_com_is_doorbell_needed(tx_ring->ena_com_io_sq, &ena_tx_ctx)) {
		ena_log_io(adapter->pdev, DBG,
		    "llq tx max burst size of queue %d achieved, writing doorbell to send burst",
		    tx_ring->que->id);
		ena_ring_tx_doorbell(tx_ring);
	}

	/* Prepare the packet's descriptors and send them to device */
	rc = ena_com_prepare_tx(io_sq, &ena_tx_ctx, &nb_hw_desc);
	if (unlikely(rc != 0)) {
		if (likely(rc == ENA_COM_NO_MEM)) {
			ena_log_io(adapter->pdev, DBG, "tx ring[%d] is out of space",
			    tx_ring->que->id);
		} else {
			ena_log(adapter->pdev, ERR, "failed to prepare tx bufs");
			ena_trigger_reset(adapter,
			    ENA_REGS_RESET_DRIVER_INVALID_STATE);
		}
		counter_u64_add(tx_ring->tx_stats.prepare_ctx_err, 1);
		goto dma_error;
	}

	counter_enter();
	counter_u64_add_protected(tx_ring->tx_stats.cnt, 1);
	counter_u64_add_protected(tx_ring->tx_stats.bytes,
	    (*mbuf)->M_dat.MH.MH_pkthdr.len);

	counter_u64_add_protected(adapter->hw_stats.tx_packets, 1);
	counter_u64_add_protected(adapter->hw_stats.tx_bytes,
	    (*mbuf)->M_dat.MH.MH_pkthdr.len);
	counter_exit();

	tx_info->tx_descs = nb_hw_desc;
	tx_info->timestamp = osv::clock::uptime::now().time_since_epoch().count();
	tx_info->print_once = true;

	tx_ring->next_to_use = ENA_TX_RING_IDX_NEXT(next_to_use,
	    tx_ring->ring_size);

	/* stop the queue when no more space available, the packet can have up
	 * to sgl_size + 2. one for the meta descriptor and one for header
	 * (if the header is larger than tx_max_header_size).
	 */
	if (unlikely(!ena_com_sq_have_enough_space(tx_ring->ena_com_io_sq,
	    adapter->max_tx_sgl_size + 2))) {
		ena_log_io(adapter->pdev, DBG, "Stop queue %d", tx_ring->que->id);

		tx_ring->running = false;
		counter_u64_add(tx_ring->tx_stats.queue_stop, 1);

		/* There is a rare condition where this function decides to
		 * stop the queue but meanwhile tx_cleanup() updates
		 * next_to_completion and terminates.
		 * The queue will remain stopped forever.
		 * To solve this issue this function performs mb(), checks
		 * the wakeup condition and wakes up the queue if needed.
		 */
		mb();

		if (ena_com_sq_have_enough_space(tx_ring->ena_com_io_sq,
		    ENA_TX_RESUME_THRESH)) {
			tx_ring->running = true;
			counter_u64_add(tx_ring->tx_stats.queue_wakeup, 1);
		}
	}

	return (0);

dma_error:
	tx_info->mbuf = NULL;

	return (rc);
}

static void
ena_start_xmit(struct ena_ring *tx_ring)
{
	struct mbuf *mbuf;
	struct ena_adapter *adapter = tx_ring->adapter;
	int ret = 0;
	int mnum = 0;

	ENA_RING_MTX_ASSERT(tx_ring);

	if (unlikely((adapter->ifp->if_drv_flags & IFF_DRV_RUNNING) == 0))
		return;

	if (unlikely(!ENA_FLAG_ISSET(ENA_FLAG_LINK_UP, adapter)))
		return;

	while ((mbuf = static_cast<struct mbuf *>(buf_ring_peek_clear_sc(tx_ring->br))) != NULL) {
		ena_log_io(adapter->pdev, DBG,
		    "dequeued mbuf %p with flags %#x and header csum flags %#jx",
		    mbuf, mbuf->m_hdr.mh_flags, (uint64_t)mbuf->M_dat.MH.MH_pkthdr.csum_flags);

		if (unlikely(!tx_ring->running)) {
			buf_ring_putback_sc(tx_ring->br, mbuf);
			break;
		}

		if (unlikely((ret = ena_xmit_mbuf(tx_ring, &mbuf)) != 0)) {
			if (ret == ENA_COM_NO_MEM) {
				buf_ring_putback_sc(tx_ring->br, mbuf);
			} else if (ret == ENA_COM_NO_SPACE) {
				buf_ring_putback_sc(tx_ring->br, mbuf);
			} else {
				m_freem(mbuf);
				buf_ring_advance_sc(tx_ring->br);
			}

			break;
		}

		buf_ring_advance_sc(tx_ring->br);

		if (unlikely((adapter->ifp->if_drv_flags & IFF_DRV_RUNNING) == 0))
			return;

		tx_ring->acum_pkts++;

		BPF_MTAP(adapter->ifp, mbuf);
		mnum++;
	}

	ena_log_io(adapter->pdev, INFO,
		"dequeued %d mbufs", mnum);

	if (likely(tx_ring->acum_pkts != 0)) {
		/* Trigger the dma engine */
		ena_ring_tx_doorbell(tx_ring);
		ena_log(adapter->pdev, DBG, "Rang TX doorbell, tx_ring->running:%d", tx_ring->running);
	}

	if (unlikely(!tx_ring->running))
		tx_ring->que->cleanup_thread->wake_with([tx_ring] { tx_ring->que->cleanup_pending++; });
}
