#ifndef OSV_BITS_STAT_H
#define OSV_BITS_STAT_H

/* for aarch64 moved padding to keep stuff 8-aligned */
struct stat {
    dev_t st_dev;
    ino_t st_ino;
    nlink_t st_nlink;
    mode_t st_mode;
    uid_t st_uid;
    gid_t st_gid;
    dev_t st_rdev;
    off_t st_size;

    blksize_t st_blksize; /* blksize_t is int */
    unsigned int __pad0;  /* therefore pad here */

    blkcnt_t st_blocks;
    struct timespec st_atim;
    struct timespec st_mtim;
    struct timespec st_ctim;
    long __unused[2];
};

#endif /* OSV_BITS_STAT_H */
