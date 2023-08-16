#ifndef	_SYS_STATX_H
#define	_SYS_STATX_H
#ifdef __cplusplus
extern "C" {
#endif

#define STATX_BASIC_STATS 0x000007ffU

struct statx_timestamp
{
    int64_t tv_sec;
    uint32_t tv_nsec;
    int32_t statx_timestamp_pad1[1];
};

struct statx {
    uint32_t stx_mask;
    uint32_t stx_blksize;
    uint64_t stx_attributes;
    uint32_t stx_nlink;
    uint32_t stx_uid;
    uint32_t stx_gid;
    uint16_t stx_mode;
    uint16_t __statx_pad1[1];
    uint64_t stx_ino;
    uint64_t stx_size;
    uint64_t stx_blocks;
    uint64_t stx_attributes_mask;
    struct statx_timestamp stx_atime;
    struct statx_timestamp stx_btime;
    struct statx_timestamp stx_ctime;
    struct statx_timestamp stx_mtime;
    uint32_t stx_rdev_major;
    uint32_t stx_rdev_minor;
    uint32_t stx_dev_major;
    uint32_t stx_dev_minor;
    uint64_t __statx_pad2[14];
};

extern "C" int statx(int dirfd, const char* pathname, int flags, unsigned int mask, struct statx *buf);

#ifdef __cplusplus
}
#endif
#endif
