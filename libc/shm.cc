/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <sys/mman.h>
#include <sys/shm.h>
#include <osv/mmu.hh>
#include <osv/fcntl.h>
#include <osv/align.hh>
#include <unordered_map>
#include <fs/fs.hh>
#include <libc/libc.hh>

static mutex shm_lock;

// Maps from custom key to shmid. IPC_PRIVATE are not stored here.
static std::unordered_map<key_t, int> shmkeys;
// Maps from mapped address to shmid. Used for detach.
static std::unordered_map<const void*, int> shmmap;

void *shmat(int shmid, const void *shmaddr, int shmflg)
{
#ifdef AARCH64_PORT_STUB
    abort();
#else /* !AARCH64_PORT_STUB */
    fileref f(fileref_from_fd(shmid));
    void *addr;
    try {
        addr = mmu::map_file(shmaddr, size_t(::size(f)), mmu::mmap_shared | (shmaddr ? mmu::mmap_fixed : 0),
                (shmflg & SHM_RDONLY) ? mmu::perm_read : mmu::perm_rw, f, 0);
    } catch (error err) {
        err.to_libc(); // sets errno
        return MAP_FAILED;
    }
    WITH_LOCK(shm_lock) {
        shmmap.emplace(addr, shmid);
    }
    return addr;
#endif /* !AARCH64_PORT_STUB */
}

int shmctl(int shmid, int cmd, struct shmid_ds *buf)
{
    if (cmd == IPC_RMID) {
        close(shmid);
        return 0;
    }
    return libc_error(EINVAL);
}

int shmdt(const void *shmaddr)
{
#ifdef AARCH64_PORT_STUB
    abort();
#else /* !AARCH64_PORT_STUB */
    int fd;
    WITH_LOCK(shm_lock) {
        auto s = shmmap.find(shmaddr);
        if (s == shmmap.end()) {
            return libc_error(EINVAL);
        }
        shmmap.erase(s);
        fd = s->second;
    }
    fileref f(fileref_from_fd(fd));
    mmu::munmap(shmaddr, ::size(f));
    return 0;
#endif /* !AARCH64_PORT_STUB */
}

/*
 * shm is implemented on top of shared memory file.
 * shmget returns shm file descriptor as shmid.
 */
int shmget(key_t key, size_t size, int shmflg)
{
#ifdef AARCH64_PORT_STUB
    abort();
#else
    int fd;
    int flags = FREAD | FWRITE;
    size = align_up(size, mmu::page_size);
    SCOPE_LOCK(shm_lock);

    try {
        if (key == IPC_PRIVATE) {
            fileref fref = make_file<mmu::shm_file>(size, flags);
            fdesc f(fref);
            fd = f.release();
        } else {
            auto s = shmkeys.find(key);
            if (s == shmkeys.end() && (shmflg & IPC_CREAT)) {
                fileref fref = make_file<mmu::shm_file>(size, flags);
                fdesc f(fref);
                fd = f.release();
                shmkeys.emplace(key, fd);
            } else if ((shmflg & (IPC_CREAT | IPC_EXCL)) == (IPC_CREAT | IPC_EXCL)) {
                return libc_error(EEXIST);
            } else {
                fd = s->second;
            }
        }
    } catch (int error) {
        return libc_error(error);
    }
    return fd;
#endif /* !AARCH64_PORT_STUB */
}
