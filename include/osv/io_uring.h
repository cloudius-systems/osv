/*
 * Copyright (C) 2026 OSv Developers
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef OSV_IO_URING_H
#define OSV_IO_URING_H

#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* io_uring operation codes - matches Linux kernel ABI */
enum {
    IORING_OP_NOP,              /*  0 */
    IORING_OP_READV,            /*  1 */
    IORING_OP_WRITEV,           /*  2 */
    IORING_OP_FSYNC,            /*  3 */
    IORING_OP_READ_FIXED,       /*  4 */
    IORING_OP_WRITE_FIXED,      /*  5 */
    IORING_OP_POLL_ADD,         /*  6 */
    IORING_OP_POLL_REMOVE,      /*  7 */
    IORING_OP_SYNC_FILE_RANGE,  /*  8 */
    IORING_OP_SENDMSG,          /*  9 */
    IORING_OP_RECVMSG,          /* 10 */
    IORING_OP_TIMEOUT,          /* 11 */
    IORING_OP_TIMEOUT_REMOVE,   /* 12 */
    IORING_OP_ACCEPT,           /* 13 */
    IORING_OP_ASYNC_CANCEL,     /* 14 */
    IORING_OP_LINK_TIMEOUT,     /* 15 */
    IORING_OP_CONNECT,          /* 16 */
    IORING_OP_FALLOCATE,        /* 17 */
    IORING_OP_OPENAT,           /* 18 */
    IORING_OP_CLOSE,            /* 19 */
    IORING_OP_FILES_UPDATE,     /* 20 */
    IORING_OP_STATX,            /* 21 */
    IORING_OP_READ,             /* 22 */
    IORING_OP_WRITE,            /* 23 */
    IORING_OP_FADVISE,          /* 24 */
    IORING_OP_MADVISE,          /* 25 */
    IORING_OP_SEND,             /* 26 */
    IORING_OP_RECV,             /* 27 */
    IORING_OP_OPENAT2,          /* 28 */
    IORING_OP_EPOLL_CTL,        /* 29 */
    IORING_OP_SPLICE,           /* 30 */
    IORING_OP_PROVIDE_BUFFERS,  /* 31 */
    IORING_OP_REMOVE_BUFFERS,   /* 32 */
    IORING_OP_TEE,              /* 33 */
    IORING_OP_SHUTDOWN,         /* 34 */
    IORING_OP_RENAMEAT,         /* 35 */
    IORING_OP_UNLINKAT,         /* 36 */
    IORING_OP_MKDIRAT,          /* 37 */
    IORING_OP_SYMLINKAT,        /* 38 */
    IORING_OP_LINKAT,           /* 39 */
    IORING_OP_LAST,             /* 40 - not a real op, used as sentinel */
};

/* SQE flags (sqe->flags) */
#define IOSQE_FIXED_FILE        (1U << 0)   /* fd is a registered file index */
#define IOSQE_IO_DRAIN          (1U << 1)   /* issue after all inflight I/O */
#define IOSQE_IO_LINK           (1U << 2)   /* link to next sqe on success */
#define IOSQE_IO_HARDLINK       (1U << 3)   /* link to next sqe unconditionally */
#define IOSQE_ASYNC             (1U << 4)   /* always dispatch asynchronously */
#define IOSQE_BUFFER_SELECT     (1U << 5)   /* select buffer from sqe->buf_group */
#define IOSQE_CQE_SKIP_SUCCESS  (1U << 6)   /* suppress CQE on success */

/* io_uring_setup() flags */
#define IORING_SETUP_IOPOLL     (1U << 0)
#define IORING_SETUP_SQPOLL     (1U << 1)
#define IORING_SETUP_SQ_AFF     (1U << 2)
#define IORING_SETUP_CQSIZE     (1U << 3)
#define IORING_SETUP_CLAMP      (1U << 4)
#define IORING_SETUP_ATTACH_WQ  (1U << 5)

/* Feature flags returned in io_uring_params.features */
#define IORING_FEAT_SINGLE_MMAP         (1U << 0)
#define IORING_FEAT_NODROP              (1U << 1)
#define IORING_FEAT_SUBMIT_STABLE       (1U << 2)
#define IORING_FEAT_RW_CUR_POS          (1U << 3)
#define IORING_FEAT_CUR_PERSONALITY     (1U << 4)
#define IORING_FEAT_FAST_POLL           (1U << 5)
#define IORING_FEAT_POLL_32BITS         (1U << 6)
#define IORING_FEAT_SQPOLL_NONFIXED     (1U << 7)
#define IORING_FEAT_EXT_ARG             (1U << 8)
#define IORING_FEAT_NATIVE_WORKERS      (1U << 9)

/* SQ ring flags (sq_ring->flags, written by kernel) */
#define IORING_SQ_NEED_WAKEUP   (1U << 0)
#define IORING_SQ_CQ_OVERFLOW   (1U << 1)

/* io_uring_enter() flags */
#define IORING_ENTER_GETEVENTS  (1U << 0)
#define IORING_ENTER_SQ_WAKEUP  (1U << 1)

/* CQE flags */
#define IORING_CQE_F_BUFFER     (1U << 0)   /* upper 16 bits are buffer index */
#define IORING_CQE_F_MORE       (1U << 1)   /* more completions follow (multishot) */

/* Timeout flags (sqe->timeout_flags) */
#define IORING_TIMEOUT_ABS              (1U << 0)
#define IORING_TIMEOUT_UPDATE           (1U << 1)
#define IORING_TIMEOUT_BOOTTIME         (1U << 2)
#define IORING_TIMEOUT_REALTIME         (1U << 3)
#define IORING_TIMEOUT_CLOCK_MASK       (IORING_TIMEOUT_BOOTTIME|IORING_TIMEOUT_REALTIME)
#define IORING_TIMEOUT_ETIME_SUCCESS    (1U << 5)
#define IORING_TIMEOUT_MULTISHOT        (1U << 6)

/* SPLICE_F_FD_IN_FIXED: fd_in for IORING_OP_SPLICE is a registered file */
#define SPLICE_F_FD_IN_FIXED    (1U << 31)

/* Submission Queue Entry */
struct io_uring_sqe {
    uint8_t  opcode;
    uint8_t  flags;         /* IOSQE_* */
    uint16_t ioprio;
    int32_t  fd;
    union {
        uint64_t off;
        uint64_t addr2;
    };
    union {
        uint64_t addr;
        uint64_t splice_off_in;
    };
    uint32_t len;
    union {
        uint32_t rw_flags;
        uint32_t fsync_flags;
        uint16_t poll_events;
        uint32_t poll32_events;
        uint32_t sync_range_flags;
        uint32_t msg_flags;
        uint32_t timeout_flags;
        uint32_t accept_flags;
        uint32_t cancel_flags;
        uint32_t open_flags;
        uint32_t statx_flags;
        uint32_t fadvise_advice;
        uint32_t splice_flags;
        uint32_t rename_flags;
        uint32_t unlink_flags;
        uint32_t hardlink_flags;
    };
    uint64_t user_data;
    union {
        struct {
            union {
                uint16_t buf_index;
                uint16_t buf_group;
            };
            uint16_t personality;
            int32_t  splice_fd_in;
        };
        uint64_t __pad2[3];
    };
};

/* Completion Queue Entry */
struct io_uring_cqe {
    uint64_t user_data;
    int32_t  res;
    uint32_t flags;
};

/* Submission queue ring embedded in the SQ mmap region */
struct io_uring_sq_ring {
    uint32_t head;
    uint32_t tail;
    uint32_t ring_mask;
    uint32_t ring_entries;
    uint32_t flags;
    uint32_t dropped;
    uint32_t array[];       /* SQE index array, length = ring_entries */
};

/* Completion queue ring embedded in the CQ mmap region */
struct io_uring_cq_ring {
    uint32_t head;
    uint32_t tail;
    uint32_t ring_mask;
    uint32_t ring_entries;
    uint32_t overflow;
    struct io_uring_cqe cqes[];
};

/* Field offsets for the SQ ring mmap (reported in io_uring_params.sq_off) */
struct io_sqring_offsets {
    uint32_t head;
    uint32_t tail;
    uint32_t ring_mask;
    uint32_t ring_entries;
    uint32_t flags;
    uint32_t dropped;
    uint32_t array;
    uint32_t resv1;
    uint64_t resv2;
};

/* Field offsets for the CQ ring mmap (reported in io_uring_params.cq_off) */
struct io_cqring_offsets {
    uint32_t head;
    uint32_t tail;
    uint32_t ring_mask;
    uint32_t ring_entries;
    uint32_t overflow;
    uint32_t cqes;
    uint32_t flags;
    uint32_t resv1;
    uint64_t resv2;
};

/* io_uring_setup() parameter block - binary-compatible with Linux 5.4+ ABI */
struct io_uring_params {
    uint32_t sq_entries;
    uint32_t cq_entries;
    uint32_t flags;
    uint32_t sq_thread_cpu;
    uint32_t sq_thread_idle;
    uint32_t features;
    uint32_t wq_fd;
    uint32_t resv[3];
    struct io_sqring_offsets sq_off;
    struct io_cqring_offsets cq_off;
};

/* io_uring_register() opcodes */
enum {
    IORING_REGISTER_BUFFERS             =  0,
    IORING_UNREGISTER_BUFFERS           =  1,
    IORING_REGISTER_FILES               =  2,
    IORING_UNREGISTER_FILES             =  3,
    IORING_REGISTER_EVENTFD             =  4,
    IORING_UNREGISTER_EVENTFD           =  5,
    IORING_REGISTER_FILES_UPDATE        =  6,
    IORING_REGISTER_EVENTFD_ASYNC       =  7,
    IORING_REGISTER_PROBE               =  8,
    IORING_REGISTER_PERSONALITY         =  9,
    IORING_UNREGISTER_PERSONALITY       = 10,
    IORING_REGISTER_RESTRICTIONS        = 11,
    IORING_REGISTER_ENABLE_RINGS        = 12,
    IORING_REGISTER_FILES2              = 13,
    IORING_REGISTER_FILES_UPDATE2       = 14,
    IORING_REGISTER_BUFFERS2            = 15,
    IORING_REGISTER_BUFFERS_UPDATE      = 16,
    IORING_REGISTER_IOWQ_AFF            = 17,
    IORING_UNREGISTER_IOWQ_AFF         = 18,
    IORING_REGISTER_IOWQ_MAX_WORKERS    = 19,
    IORING_REGISTER_RING_FDS            = 20,
    IORING_UNREGISTER_RING_FDS         = 21,
    IORING_REGISTER_PBUF_RING           = 22,
    IORING_UNREGISTER_PBUF_RING        = 23,
    IORING_REGISTER_SYNC_CANCEL         = 24,
};

/* Probe operation entry for IORING_REGISTER_PROBE */
struct io_uring_probe_op {
    uint8_t  op;
    uint8_t  resv;
    uint16_t flags;     /* IO_URING_OP_SUPPORTED if the op is available */
    uint32_t resv2;
};

#define IO_URING_OP_SUPPORTED   (1U << 0)

/* Probe structure passed to IORING_REGISTER_PROBE */
struct io_uring_probe {
    uint8_t  last_op;   /* highest supported opcode */
    uint8_t  ops_len;   /* number of entries in ops[] */
    uint16_t resv;
    uint32_t resv2[3];
    struct io_uring_probe_op ops[];
};

/* Kernel-compatible timespec used by IORING_OP_TIMEOUT */
struct __kernel_timespec {
    int64_t tv_sec;
    int64_t tv_nsec;
};

/* Argument for IORING_OP_OPENAT2 */
struct open_how {
    uint64_t flags;
    uint64_t mode;
    uint64_t resolve;
};

/* Single provided buffer entry */
struct io_uring_buf {
    uint64_t addr;
    uint32_t len;
    uint16_t bid;
    uint16_t resv;
};

/* Provided buffer ring layout (one ring per buffer group) */
struct io_uring_buf_ring {
    union {
        struct {
            uint64_t resv1;
            uint32_t resv2;
            uint16_t resv3;
            uint16_t tail;
        };
        /*
         * Zero-length array (GCC/Clang extension) so this compiles in C++
         * mode.  The ABI matches the flexible-array version: bufs[0] is at
         * offset 0 within the union, i.e. at the ring base address.
         */
        struct io_uring_buf bufs[0];
    };
};

/* Argument for IORING_REGISTER_PBUF_RING */
struct io_uring_buf_reg {
    uint64_t ring_addr;
    uint32_t ring_entries;
    uint16_t bgid;
    uint16_t pad;
    uint64_t resv[3];
};

/* Argument for IORING_REGISTER_FILES_UPDATE */
struct io_uring_files_update {
    uint32_t offset;
    uint32_t resv;
    uint64_t fds;
};

/* Argument for IORING_REGISTER_SYNC_CANCEL */
struct io_uring_sync_cancel_reg {
    uint64_t addr;
    int32_t  fd;
    uint32_t flags;
    struct __kernel_timespec timeout;
    uint64_t pad[4];
};

/* System call entry points */
long sys_io_uring_setup(unsigned entries, struct io_uring_params *params);
int  sys_io_uring_enter(int fd, unsigned to_submit, unsigned min_complete,
                        unsigned flags, const void *sig, size_t sigsz);
int  sys_io_uring_register(int fd, unsigned opcode, void *arg, unsigned nr_args);

#ifdef __cplusplus
}
#endif

#endif /* OSV_IO_URING_H */
