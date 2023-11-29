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
 *
 * $FreeBSD$
 *
 */

#ifndef ENA_H
#define ENA_H


#include "ena_com/ena_com.h"
#include "ena_com/ena_eth_com.h"

#include <bsd/porting/callout.h>
#include <osv/msi.hh>
#include "drivers/pci-device.hh"

#define ENA_DRV_MODULE_VER_MAJOR	2
#define ENA_DRV_MODULE_VER_MINOR	6
#define ENA_DRV_MODULE_VER_SUBMINOR	3

#define ENA_DRV_MODULE_NAME		"ena"

#define	__STRING(x)	#x		/* stringify without expanding x */
#define	__XSTRING(x)	__STRING(x)	/* expand x, then stringify */

#ifndef ENA_DRV_MODULE_VERSION
#define ENA_DRV_MODULE_VERSION				\
	__XSTRING(ENA_DRV_MODULE_VER_MAJOR) "."		\
	__XSTRING(ENA_DRV_MODULE_VER_MINOR) "."		\
	__XSTRING(ENA_DRV_MODULE_VER_SUBMINOR)
#endif
#define ENA_DEVICE_NAME	"Elastic Network Adapter (ENA)"
#define ENA_DEVICE_DESC	"ENA adapter"

/* Calculate DMA mask - width for ena cannot exceed 48, so it is safe */
#define ENA_DMA_BIT_MASK(x)		((1ULL << (x)) - 1ULL)

/* 1 for AENQ + ADMIN */
#define ENA_ADMIN_MSIX_VEC		1
#define ENA_MAX_MSIX_VEC(io_queues)	(ENA_ADMIN_MSIX_VEC + (io_queues))

#define ENA_REG_BAR			0
#define ENA_MEM_BAR			2

#define ENA_BUS_DMA_SEGS		32

#define ENA_DEFAULT_BUF_RING_SIZE	4096

#define ENA_DEFAULT_RING_SIZE		1024
#define ENA_MIN_RING_SIZE		256

/*
 * Refill Rx queue when number of required descriptors is above
 * QUEUE_SIZE / ENA_RX_REFILL_THRESH_DIVIDER or ENA_RX_REFILL_THRESH_PACKET
 */
#define ENA_RX_REFILL_THRESH_DIVIDER	8
#define ENA_RX_REFILL_THRESH_PACKET	256

#define ENA_IRQNAME_SIZE		40

#define ENA_PKT_MAX_BUFS 		19

#define ENA_RX_RSS_TABLE_LOG_SIZE	7
#define ENA_RX_RSS_TABLE_SIZE		(1 << ENA_RX_RSS_TABLE_LOG_SIZE)

#define ENA_HASH_KEY_SIZE		40

#define ENA_MAX_FRAME_LEN		10000
#define ENA_MIN_FRAME_LEN 		60

#define ENA_TX_RESUME_THRESH		(ENA_PKT_MAX_BUFS + 2)

#define ENA_DB_THRESHOLD	64

#define ENA_TX_COMMIT	32
 /*
 * TX budget for cleaning. It should be half of the RX budget to reduce amount
 *  of TCP retransmissions.
 */
#define ENA_TX_BUDGET	128
/* RX cleanup budget. -1 stands for infinity. */
#define ENA_RX_BUDGET	256
/*
 * How many times we can repeat cleanup in the io irq handling routine if the
 * RX or TX budget was depleted.
 */
#define ENA_CLEAN_BUDGET	8

#define ENA_RX_IRQ_INTERVAL	20
#define ENA_TX_IRQ_INTERVAL	50

#define ENA_MIN_MTU		128

#define ENA_TSO_MAXSIZE		65536

#define ENA_MMIO_DISABLE_REG_READ	BIT(0)

#define ENA_TX_RING_IDX_NEXT(idx, ring_size) (((idx) + 1) & ((ring_size) - 1))

#define ENA_RX_RING_IDX_NEXT(idx, ring_size) (((idx) + 1) & ((ring_size) - 1))

#define ENA_IO_TXQ_IDX(q)		(2 * (q))
#define ENA_IO_RXQ_IDX(q)		(2 * (q) + 1)
#define ENA_IO_TXQ_IDX_TO_COMBINED_IDX(q)	((q) / 2)
#define ENA_IO_RXQ_IDX_TO_COMBINED_IDX(q)	(((q) - 1) / 2)

#define ENA_MGMNT_IRQ_IDX		0
#define ENA_IO_IRQ_FIRST_IDX		1
#define ENA_IO_IRQ_IDX(q)		(ENA_IO_IRQ_FIRST_IDX + (q))

#define ENA_MAX_NO_INTERRUPT_ITERATIONS	3

/*
 * ENA device should send keep alive msg every 1 sec.
 * We wait for 6 sec just to be on the safe side.
 */
#define NANOSECONDS_IN_SEC  1000000000l
#define NANOSECONDS_IN_MSEC 1000000l
#define ENA_DEFAULT_KEEP_ALIVE_TO	(6 * NANOSECONDS_IN_SEC)

/* Time in jiffies before concluding the transmitter is hung. */
#define ENA_DEFAULT_TX_CMP_TO		(5 * NANOSECONDS_IN_SEC)

/* Number of queues to check for missing queues per timer tick */
#define ENA_DEFAULT_TX_MONITORED_QUEUES	(4)

/* Max number of timeouted packets before device reset */
#define ENA_DEFAULT_TX_CMP_THRESHOLD	(128)

/*
 * Supported PCI vendor and devices IDs
 */
#define PCI_VENDOR_ID_AMAZON	0x1d0f

#define PCI_DEV_ID_ENA_PF		0x0ec2
#define PCI_DEV_ID_ENA_PF_RSERV0	0x1ec2
#define PCI_DEV_ID_ENA_VF		0xec20
#define PCI_DEV_ID_ENA_VF_RSERV0	0xec21

//These macros are taken verbatim from FreeBSD code and implement atomic bitset
#define	_BITSET_BITS		(sizeof(long) * 8)

#define	__howmany(x, y)	(((x) + ((y) - 1)) / (y))

#define	__bitset_words(_s)	(__howmany(_s, _BITSET_BITS))

#define	__constexpr_cond(expr)	(__builtin_constant_p((expr)) && (expr))

#define	__bitset_mask(_s, n)						\
	(1UL << (__constexpr_cond(__bitset_words((_s)) == 1) ?		\
	    (size_t)(n) : ((n) % _BITSET_BITS)))

#define	__bitset_word(_s, n)						\
	(__constexpr_cond(__bitset_words((_s)) == 1) ?			\
	 0 : ((n) / _BITSET_BITS))

#define	BITSET_DEFINE(_t, _s)						\
struct _t {								\
	long    __bits[__bitset_words((_s))];				\
}

#define	BIT_ZERO(_s, p) do {						\
	size_t __i;							\
	for (__i = 0; __i < __bitset_words((_s)); __i++)		\
		(p)->__bits[__i] = 0L;					\
} while (0)

#define	BIT_ISSET(_s, n, p)						\
	((((p)->__bits[__bitset_word(_s, n)] & __bitset_mask((_s), (n))) != 0))

#define	BIT_SET_ATOMIC(_s, n, p)					\
	atomic_set_long((volatile u_long*)(&(p)->__bits[__bitset_word(_s, n)]),	\
	    __bitset_mask((_s), n))

#define	BIT_CLR_ATOMIC(_s, n, p)					\
	atomic_clear_long((volatile u_long*)(&(p)->__bits[__bitset_word(_s, n)]),\
	    __bitset_mask((_s), n))

/*
 * Flags indicating current ENA driver state
 */
enum ena_flags_t {
	ENA_FLAG_DEVICE_RUNNING,
	ENA_FLAG_DEV_UP,
	ENA_FLAG_LINK_UP,
	ENA_FLAG_MSIX_ENABLED,
	ENA_FLAG_TRIGGER_RESET,
	ENA_FLAG_ONGOING_RESET,
	ENA_FLAG_DEV_UP_BEFORE_RESET,
	ENA_FLAG_RSS_ACTIVE,
	ENA_FLAGS_NUMBER = ENA_FLAG_RSS_ACTIVE
};

BITSET_DEFINE(_ena_state, ENA_FLAGS_NUMBER);
typedef struct _ena_state ena_state_t;

#define ENA_FLAG_ZERO(adapter)          \
	BIT_ZERO(ENA_FLAGS_NUMBER, &(adapter)->flags)
#define ENA_FLAG_ISSET(bit, adapter)    \
	BIT_ISSET(ENA_FLAGS_NUMBER, (bit), &(adapter)->flags)
#define ENA_FLAG_SET_ATOMIC(bit, adapter)	\
	BIT_SET_ATOMIC(ENA_FLAGS_NUMBER, (bit), &(adapter)->flags)
#define ENA_FLAG_CLEAR_ATOMIC(bit, adapter)	\
	BIT_CLR_ATOMIC(ENA_FLAGS_NUMBER, (bit), &(adapter)->flags)

struct msix_entry {
	int entry;
	int vector;
};

typedef struct _ena_vendor_info_t {
	uint16_t vendor_id;
	uint16_t device_id;
	unsigned int index;
} ena_vendor_info_t;

struct ena_irq {
	/* Interrupt resources */
	void *data;
	unsigned int vector;
	msix_vector *mvector;
};

struct ena_que {
	struct ena_adapter *adapter;
	struct ena_ring *tx_ring;
	struct ena_ring *rx_ring;

	sched::thread* cleanup_thread;
	std::atomic<uint16_t> cleanup_pending = {0};
	std::atomic<bool> cleanup_stop = {false};

	uint32_t id;
	int domain;
	struct sysctl_oid *oid;

};

struct ena_calc_queue_size_ctx {
	struct ena_com_dev_get_features_ctx *get_feat_ctx;
	struct ena_com_dev *ena_dev;
	pci::device *pdev;
	uint32_t tx_queue_size;
	uint32_t rx_queue_size;
	uint32_t max_tx_queue_size;
	uint32_t max_rx_queue_size;
	uint16_t max_tx_sgl_size;
	uint16_t max_rx_sgl_size;
};

struct ena_tx_buffer {
	struct mbuf *mbuf;
	/* # of ena desc for this specific mbuf
	 * (includes data desc and metadata desc) */
	unsigned int tx_descs;
	/* # of buffers used by this mbuf */
	unsigned int num_of_bufs;

	/* Used to detect missing tx packets */
	u64 timestamp;
	bool print_once;

	struct ena_com_buf bufs[ENA_PKT_MAX_BUFS];
} __aligned(CACHE_LINE_SIZE);

struct ena_rx_buffer {
	struct mbuf *mbuf;
	struct ena_com_buf ena_buf;
} __aligned(CACHE_LINE_SIZE);

//TODO: See if we need atomics or possibly these get updated/read without a need
//for locking
//In FreeBSD they seem to be atomics per CPU - see https://github.com/freebsd/freebsd-src/blob/main/sys/arm64/include/counter.h#L82
//For now disable all the counter related code -> it seems to be used only to track
//stats
typedef u64 counter_u64_t;
#define counter_u64_zero(cnt) do {} while (0)
#define counter_u64_add(cnt,inc) do {} while (0)
#define counter_u64_add_protected(cnt,inc) do {} while (0)
#define counter_enter() do {} while (0)
#define counter_exit() do {} while (0)

struct ena_stats_tx {
	counter_u64_t cnt;
	counter_u64_t bytes;
	counter_u64_t prepare_ctx_err;
	counter_u64_t doorbells;
	counter_u64_t missing_tx_comp;
	counter_u64_t bad_req_id;
	counter_u64_t collapse;
	counter_u64_t collapse_err;
	counter_u64_t queue_wakeup;
	counter_u64_t queue_stop;
	counter_u64_t llq_buffer_copy;
	counter_u64_t unmask_interrupt_num;
};

struct ena_stats_rx {
	counter_u64_t cnt;
	counter_u64_t bytes;
	counter_u64_t refil_partial;
	counter_u64_t csum_bad;
	counter_u64_t mjum_alloc_fail;
	counter_u64_t mbuf_alloc_fail;
	counter_u64_t bad_desc_num;
	counter_u64_t bad_req_id;
	counter_u64_t empty_rx_ring;
	counter_u64_t csum_good;
};

struct ena_ring {
	/* Holds the empty requests for TX/RX out of order completions */
	union {
		uint16_t *free_tx_ids;
		uint16_t *free_rx_ids;
	};
	struct ena_com_dev *ena_dev;
	struct ena_adapter *adapter;
	struct ena_com_io_cq *ena_com_io_cq;
	struct ena_com_io_sq *ena_com_io_sq;

	uint16_t qid;

	/* Determines if device will use LLQ or normal mode for TX */
	enum ena_admin_placement_policy_type tx_mem_queue_type;
	union {
		/* The maximum length the driver can push to the device (For LLQ) */
		uint8_t tx_max_header_size;
		/* The maximum (and default) mbuf size for the Rx descriptor. */
		uint16_t rx_mbuf_sz;

	};

	std::atomic<uint8_t> first_interrupt;
	uint16_t no_interrupt_event_cnt;

	struct ena_com_rx_buf_info ena_bufs[ENA_PKT_MAX_BUFS];

	struct ena_que *que;
	struct lro_ctrl lro;

	uint16_t next_to_use;
	uint16_t next_to_clean;

	union {
		struct ena_tx_buffer *tx_buffer_info; /* contex of tx packet */
		struct ena_rx_buffer *rx_buffer_info; /* contex of rx packet */
	};
	int ring_size; /* number of tx/rx_buffer_info's entries */

	struct buf_ring *br; /* only for TX */
	uint32_t buf_ring_size;

	struct mtx ring_mtx;
	char mtx_name[16];

	sched::thread* enqueue_thread;
	std::atomic<uint16_t> enqueue_pending = {0};
	std::atomic<bool> enqueue_stop = {false};

	union {
		struct ena_stats_tx tx_stats;
		struct ena_stats_rx rx_stats;
	};

	union {
		int empty_rx_queue;
		/* For Tx ring to indicate if it's running or not */
		bool running;
	};

	/* How many packets are sent in one Tx loop, used for doorbells */
	uint32_t acum_pkts;

	/* Used for LLQ */
	uint8_t *push_buf_intermediate_buf;

	int tx_last_cleanup_ticks;
} __aligned(CACHE_LINE_SIZE);

struct ena_stats_dev {
	counter_u64_t wd_expired;
	counter_u64_t interface_up;
	counter_u64_t interface_down;
	counter_u64_t admin_q_pause;
};

struct ena_hw_stats {
	counter_u64_t rx_packets;
	counter_u64_t tx_packets;

	counter_u64_t rx_bytes;
	counter_u64_t tx_bytes;

	counter_u64_t rx_drops;
	counter_u64_t tx_drops;
};

typedef struct ifnet* if_t;

/* Board specific private data structure */
struct ena_adapter {
	struct ena_com_dev *ena_dev;

	/* OS defined structs */
	if_t ifp;
	pci::device *pdev;
	struct ifmedia	media;

	/* OS resources */
	pci::bar *registers;

	/* MSI-X */
	int msix_vecs;

	uint32_t max_mtu;

	uint32_t num_io_queues;
	uint32_t max_num_io_queues;

	uint32_t requested_tx_ring_size;
	uint32_t requested_rx_ring_size;

	uint32_t max_tx_ring_size;
	uint32_t max_rx_ring_size;

	uint16_t max_tx_sgl_size;
	uint16_t max_rx_sgl_size;

	uint32_t tx_offload_cap;

	uint32_t buf_ring_size;

	/* RSS*/
	int first_bind;
	struct ena_indir *rss_indir;

	uint8_t mac_addr[ETHER_ADDR_LEN];
	/* mdio and phy*/

	ena_state_t flags;

	/* Queue will represent one TX and one RX ring */
	struct ena_que que[ENA_MAX_NUM_IO_QUEUES]
	    __aligned(CACHE_LINE_SIZE);

	/* TX */
	struct ena_ring tx_ring[ENA_MAX_NUM_IO_QUEUES]
	    __aligned(CACHE_LINE_SIZE);

	/* RX */
	struct ena_ring rx_ring[ENA_MAX_NUM_IO_QUEUES]
	    __aligned(CACHE_LINE_SIZE);

	struct ena_irq irq_tbl[ENA_MAX_MSIX_VEC(ENA_MAX_NUM_IO_QUEUES)];

	/* Timer service */
	struct callout timer_service;
	std::atomic<u64> keep_alive_timestamp;
	uint32_t next_monitored_tx_qid;
	struct task reset_task;
	struct taskqueue *reset_tq;
	int wd_active;
	u64 keep_alive_timeout;
	u64 missing_tx_timeout;
	uint32_t missing_tx_max_queues;
	uint32_t missing_tx_threshold;
	bool disable_meta_caching;

	/* Statistics */
	struct ena_stats_dev dev_stats;
	struct ena_hw_stats hw_stats;

	enum ena_regs_reset_reason_types reset_reason;
};

#define ENA_RING_MTX_LOCK(_ring)		mtx_lock(&(_ring)->ring_mtx)
#define ENA_RING_MTX_TRYLOCK(_ring)		mtx_trylock(&(_ring)->ring_mtx)
#define ENA_RING_MTX_UNLOCK(_ring)		mtx_unlock(&(_ring)->ring_mtx)
#define ENA_RING_MTX_ASSERT(_ring)		\
	mtx_assert(&(_ring)->ring_mtx, MA_OWNED)

#define ENA_LOCK_INIT()					\
	sx_init(&ena_global_lock,	"ENA global lock")
#define ENA_LOCK_DESTROY()		sx_destroy(&ena_global_lock)
#define ENA_LOCK_LOCK()			sx_xlock(&ena_global_lock)
#define ENA_LOCK_UNLOCK()		sx_xunlock(&ena_global_lock)
#define ENA_LOCK_ASSERT()		sx_assert(&ena_global_lock, SA_XLOCKED)

#define ENA_TIMER_INIT(_adapter)					\
	callout_init(&(_adapter)->timer_service, true)
#define ENA_TIMER_DRAIN(_adapter)					\
	callout_drain(&(_adapter)->timer_service)
#define ENA_TIMER_RESET(_adapter)					\
	callout_reset(&(_adapter)->timer_service, ns2ticks(NANOSECONDS_IN_SEC),	\
			ena_timer_service, (void*)(_adapter))

#define clamp_t(type, _x, min, max)	min_t(type, max_t(type, _x, min), max)
#define clamp_val(val, lo, hi)		clamp_t(__typeof(val), val, lo, hi)

extern struct sx ena_global_lock;

int	ena_up(struct ena_adapter *adapter);
void	ena_down(struct ena_adapter *adapter);
int	ena_restore_device(struct ena_adapter *adapter);
void	ena_destroy_device(struct ena_adapter *adapter, bool graceful);
int	ena_refill_rx_bufs(struct ena_ring *rx_ring, uint32_t num);
int	ena_update_buf_ring_size(struct ena_adapter *adapter,
    uint32_t new_buf_ring_size);
int	ena_update_queue_size(struct ena_adapter *adapter, uint32_t new_tx_size,
    uint32_t new_rx_size);
int	ena_update_io_queue_nb(struct ena_adapter *adapter, uint32_t new_num);

static inline int
ena_mbuf_count(struct mbuf *mbuf)
{
	int count = 1;

	while ((mbuf = mbuf->m_hdr.mh_next) != NULL)
		++count;

	return count;
}

static inline void
ena_trigger_reset(struct ena_adapter *adapter,
    enum ena_regs_reset_reason_types reset_reason)
{
	if (likely(!ENA_FLAG_ISSET(ENA_FLAG_TRIGGER_RESET, adapter))) {
		adapter->reset_reason = reset_reason;
		ENA_FLAG_SET_ATOMIC(ENA_FLAG_TRIGGER_RESET, adapter);
	}
}

static inline void
ena_ring_tx_doorbell(struct ena_ring *tx_ring)
{
	ena_com_write_sq_doorbell(tx_ring->ena_com_io_sq);
	counter_u64_add(tx_ring->tx_stats.doorbells, 1);
	tx_ring->acum_pkts = 0;
}

#endif /* !(ENA_H) */
