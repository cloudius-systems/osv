/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef VFS_FILE_HH_
#define VFS_FILE_HH_

#include <osv/file.h>

struct arc_buf;
typedef arc_buf arc_buf_t;

class vfs_file final : public file {
public:
    explicit vfs_file(unsigned flags);
    virtual int read(struct uio *uio, int flags) override;
    virtual int write(struct uio *uio, int flags) override;
    virtual int truncate(off_t len) override;
    virtual int ioctl(u_long com, void *data) override;
    virtual int poll(int events) override;
    virtual int stat(struct stat* buf) override;
    virtual int close() override;
    virtual int chmod(mode_t mode) override;
    virtual std::unique_ptr<mmu::file_vma> mmap(addr_range range, unsigned flags, unsigned perm, off_t offset) override;
    virtual bool map_page(uintptr_t offset, size_t size, mmu::hw_ptep ptep, mmu::pt_element pte, bool write, bool shared);
    virtual bool put_page(void *addr, uintptr_t offset, size_t size, mmu::hw_ptep ptep);

    void get_arcbuf(uintptr_t offset, arc_buf_t** arcbuf, void** page);
};

#endif /* VFS_FILE_HH_ */
