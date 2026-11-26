/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <sys/mman.h>
#include <sys/shm.h>
#include <time.h>
#include <unistd.h>
#include <osv/mmu.hh>
#include <osv/fcntl.h>
#include <osv/align.hh>
#include <unordered_map>
#include <string>
#include <fs/fs.hh>
#include <libc/libc.hh>

static mutex shm_lock;

// True if `shmid` (a file descriptor) is a live SysV shared memory segment.
// A shmid is just an fd here, so a stale id can name some other open file;
// shmat() and shmctl() must reject it with EINVAL rather than map or close
// that file.
static bool shm_fd_is_segment(int shmid)
{
    if (shmid < 0) {
        return false;
    }
    fileref f(fileref_from_fd(shmid));
    if (!f) {
        return false;
    }
    return dynamic_cast<mmu::shm_file*>(f.get()) != nullptr;
}

// POSIX named shared memory objects (shm_open/shm_unlink).
// Each name maps to a fileref; the underlying shm_file lives as long as
// any open file descriptor or name registration holds a reference.
static std::unordered_map<std::string, fileref> posix_shm_objects;

// Maps from custom key to shmid. IPC_PRIVATE are not stored here.
static std::unordered_map<key_t, int> shmkeys;
// Maps from mapped address to shmid. Used for detach.
static std::unordered_map<const void*, int> shmmap;
// What shmctl(IPC_STAT) reports for each segment shmget() created, keyed by
// the segment's file, from shmget() until IPC_RMID.  shm_nattch is not kept
// here: IPC_STAT counts the attachments in shmmap.  The key is never
// dereferenced.  If a segment's id is close()d rather than removed, its
// entry stays; every shm_file is created in this file, and each creation
// replaces or erases the entry for its address, so a stale one is never read.
static std::unordered_map<const file*, struct shmid_ds> shmstats;

// The IPC_STAT attributes recorded for segment `f`, or nullptr if it has
// none.  Caller holds shm_lock.
static struct shmid_ds* shm_stats_of(const fileref& f)
{
    auto s = shmstats.find(f.get());
    return s == shmstats.end() ? nullptr : &s->second;
}

// Record a new segment's attributes as Linux reports them: shm_segsz is the
// size passed to shmget(), not the page-rounded size of the backing file.
// Caller holds shm_lock.
static void shm_record_new(const fileref& f, key_t key, size_t segsz, int shmflg)
{
    struct shmid_ds ds = {};
    ds.shm_perm.__key = key;
    ds.shm_perm.mode = shmflg & 0777;
    ds.shm_segsz = segsz;
    ds.shm_cpid = getpid();
    ds.shm_ctime = time(nullptr);
    shmstats[f.get()] = ds;
}

void *shmat(int shmid, const void *shmaddr, int shmflg)
{
    // A shmid that is not a live segment is EINVAL (POSIX), not a file to map.
    if (!shm_fd_is_segment(shmid)) {
        errno = EINVAL;
        return MAP_FAILED;
    }
    // dup() the segment's file descriptor, to create another reference to
    // the underlying shared memory segment, so that after an IPC_RMID the
    // segment will survive until the last attachment is detached.
    shmid = dup(shmid);
    if (shmid < 0) {
        return MAP_FAILED;
    }

    fileref f(fileref_from_fd(shmid));
    void *addr;
    try {
        addr = mmu::map_file(shmaddr, size_t(::size(f)), mmu::mmap_shared | (shmaddr ? mmu::mmap_fixed : 0),
                (shmflg & SHM_RDONLY) ? mmu::perm_read : mmu::perm_rw, f, 0);
    } catch (error err) {
        err.to_libc(); // sets errno
        close(shmid); // close the duplicate we created above
        return MAP_FAILED;
    }
    WITH_LOCK(shm_lock) {
        shmmap.emplace(addr, shmid);
        if (auto ds = shm_stats_of(f)) {
            ds->shm_atime = time(nullptr);
            ds->shm_lpid = getpid();
        }
    }
    return addr;
}

int shmctl(int shmid, int cmd, struct shmid_ds *buf)
{
    if (cmd == IPC_RMID) {
        if (!shm_fd_is_segment(shmid)) {
            return libc_error(EINVAL);
        }
        WITH_LOCK(shm_lock) {
            shmstats.erase(fileref_from_fd(shmid).get());
            // Forget the segment's key, so that a later shmget() with this key
            // creates a new segment instead of returning this shmid, whose fd
            // we close here and which may then be reused by an unrelated file.
            for (auto it = shmkeys.begin(); it != shmkeys.end(); ++it) {
                if (it->second == shmid) {
                    shmkeys.erase(it);
                    break;
                }
            }
        }
        close(shmid);
        return 0;
    }
    if (cmd == IPC_STAT) {
        if (!buf) {
            return libc_error(EFAULT);
        }
        if (!shm_fd_is_segment(shmid)) {
            return libc_error(EINVAL);
        }
        fileref seg(fileref_from_fd(shmid));
        memset(buf, 0, sizeof(*buf));
        SCOPE_LOCK(shm_lock);
        if (auto ds = shm_stats_of(seg)) {
            *buf = *ds;
        }
        // Each attachment holds its own dup() of the segment's fd, so count
        // the attachments whose fd refers to the same file as shmid.
        shmatt_t nattch = 0;
        for (auto& e : shmmap) {
            fileref f(fileref_from_fd(e.second));
            if (f.get() == seg.get()) {
                nattch++;
            }
        }
        buf->shm_nattch = nattch;
        return 0;
    }
    return libc_error(EINVAL);
}

int shmdt(const void *shmaddr)
{
    int fd;
    WITH_LOCK(shm_lock) {
        auto s = shmmap.find(shmaddr);
        if (s == shmmap.end()) {
            return libc_error(EINVAL);
        }
        fd = s->second;
        shmmap.erase(s);
        if (auto ds = shm_stats_of(fileref_from_fd(fd))) {
            ds->shm_dtime = time(nullptr);
            ds->shm_lpid = getpid();
        }
    }
    fileref f(fileref_from_fd(fd));
    mmu::munmap(shmaddr, ::size(f));
    // Close fd. It is a dup(), so the actual underlying shared memory
    // segment will not be destroyed until the other duplicates are closed
    // (i.e., IPC_RMID is used, and other attachments are detached).
    close(fd);
    return 0;
}

/*
 * shm is implemented on top of shared memory file.
 * shmget returns shm file descriptor as shmid.
 */
int shmget(key_t key, size_t size, int shmflg)
{
    int fd;
    int flags = FREAD | FWRITE;
    size_t segsz = size;
    size = align_up(size, mmu::page_size);
    SCOPE_LOCK(shm_lock);

    try {
        if (key == IPC_PRIVATE) {
            fileref fref = make_file<mmu::shm_file>(size, flags);
            fdesc f(fref);
            fd = f.release();
            shm_record_new(fref, key, segsz, shmflg);
        } else {
            auto s = shmkeys.find(key);
            if (s == shmkeys.end() && (shmflg & IPC_CREAT)) {
                fileref fref = make_file<mmu::shm_file>(size, flags);
                fdesc f(fref);
                fd = f.release();
                shmkeys.emplace(key, fd);
                shm_record_new(fref, key, segsz, shmflg);
            } else if (s == shmkeys.end()) {
                return libc_error(ENOENT);
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
}

/*
 * POSIX shared memory — shm_open / shm_unlink.
 *
 * Implemented on top of OSv's mmu::shm_file.  The named registry
 * (posix_shm_objects) keeps one fileref alive per name so that two
 * independent shm_open() calls with the same name share the same object.
 * The actual shm_file is freed when all file descriptors are closed AND the
 * name has been removed by shm_unlink().
 */
int shm_open(const char *name, int oflag, mode_t mode)
{
    if (!name || name[0] != '/') {
        return libc_error(EINVAL);
    }
    std::string key(name);
    SCOPE_LOCK(shm_lock);

    auto it = posix_shm_objects.find(key);
    bool exists = (it != posix_shm_objects.end());

    if ((oflag & O_CREAT) && (oflag & O_EXCL) && exists) {
        return libc_error(EEXIST);
    }
    if (!(oflag & O_CREAT) && !exists) {
        return libc_error(ENOENT);
    }

    fileref fref;
    if (!exists) {
        int fflags = FREAD | FWRITE;
        // Size 0; caller must ftruncate() before mapping.
        fref = make_file<mmu::shm_file>((size_t)0, fflags);
        shmstats.erase(fref.get());
        posix_shm_objects[key] = fref;
    } else {
        fref = it->second;
    }

    try {
        fdesc fdesc(fref);
        return fdesc.release();
    } catch (...) {
        return libc_error(EMFILE);
    }
}

int shm_unlink(const char *name)
{
    if (!name || name[0] != '/') {
        return libc_error(EINVAL);
    }
    std::string key(name);
    SCOPE_LOCK(shm_lock);
    auto it = posix_shm_objects.find(key);
    if (it == posix_shm_objects.end()) {
        return libc_error(ENOENT);
    }
    posix_shm_objects.erase(it);
    return 0;
}
