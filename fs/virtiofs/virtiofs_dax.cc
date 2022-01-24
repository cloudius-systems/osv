/*
 * Copyright (C) 2020 Fotis Xenakis
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <algorithm>
#include <mutex>

#include <api/assert.h>
#include <osv/debug.h>
#include <osv/uio.h>

#include "fuse_kernel.h"
#include "virtiofs.hh"
#include "virtiofs_dax.hh"
#include "virtiofs_i.hh"

namespace virtiofs {

template<typename W>
int dax_manager<W>::read(virtiofs_inode& inode, uint64_t file_handle,
    u64 read_amt, struct uio& uio, bool aggressive)
{
    std::lock_guard<mutex> guard {_lock};

    // Necessary pre-declarations due to goto below
    size_t to_map;
    chunk nchunks;
    int error;
    mapping_part mp;
    chunk fstart = uio.uio_offset / _chunk_size;
    off_t coffset = uio.uio_offset % _chunk_size; // offset within chunk
    if (find(inode.nodeid, fstart, mp)) {
        // Requested data (at least some initial) is already mapped
        auto read_amt_act = std::min<size_t>(read_amt,
            (mp.nchunks * _chunk_size) - coffset);
        virtiofs_debug("inode %lld, found in DAX (foffset=%lld, len=%lld, "
            "moffset=%lld)\n", inode.nodeid, uio.uio_offset, read_amt_act,
            (mp.mstart * _chunk_size) + coffset);
        goto out;
    }

    // Map file
    to_map = coffset; // bytes to map
    if (aggressive) {
        // Map the rest of the file
        to_map += inode.attr.size - uio.uio_offset;
    } else {
        // Map just enough chunks to satisfy read_amt
        to_map += read_amt;
    }
    nchunks = to_map / _chunk_size;
    if (to_map % _chunk_size > 0) {
        nchunks++;
    }
    // NOTE: This relies on the fact that requesting a mapping longer than the
    // remaining file works (see mmap() on the host). If that didn't work, we
    // would have to request exact mappings (byte-granularity, rather than
    // chunk-granularity).
    error = map(inode.nodeid, file_handle, nchunks, fstart, mp, true);
    if (error) {
        return error;
    }

out:
    auto req_data = _window.data() + (mp.mstart * _chunk_size) + coffset;
    auto read_amt_act = std::min<size_t>(read_amt,
        (mp.nchunks * _chunk_size) - coffset);
    // NOTE: It shouldn't be necessary to use the mmio* interface (i.e. volatile
    // accesses). From the spec: "Drivers map this shared memory region with
    // writeback caching as if it were regular RAM."
    error = uiomove(const_cast<void*>(req_data), read_amt_act, &uio);
    if (error) {
        kprintf("[virtiofs] inode %lld, uiomove failed\n", inode.nodeid);
    }
    return error;
}

template<typename W>
int dax_manager<W>::map(uint64_t nodeid, uint64_t file_handle, chunk nchunks,
    chunk fstart, mapping_part& mapped, bool evict)
{
    // If necessary, unmap just enough chunks
    auto empty = _window_chunks - first_empty();
    if (evict && empty < nchunks) {
        mapping_part mp;
        auto error = unmap(nchunks - empty, mp, false);
        if (error) {
            return error;
        }
        empty += mp.nchunks;
    }
    auto to_map = std::min<chunk>(nchunks, empty);
    if (to_map == 0) {
        // The window is full and evict is false, or nchunks is 0
        mapped.mstart = _window_chunks - empty;
        mapped.nchunks = 0;
        return (nchunks == 0) ? 0 : ENOBUFS;
    }

    // Map new chunks
    auto mstart = _window_chunks - empty;
    auto error = _window.map(nodeid, file_handle, to_map * _chunk_size,
        fstart * _chunk_size, mstart * _chunk_size);
    if (error) {
        return error;
    }
    if (!_mappings.empty()) {
        auto& m {_mappings.back()};
        if (m.nodeid == nodeid && m.fstart + m.nchunks == fstart) {
            // Extend previous mapping
            m.nchunks += to_map;
            mapped.mstart = mstart;
            mapped.nchunks = to_map;
            return 0;
        }
    }
    _mappings.emplace_back(nodeid, to_map, fstart, mstart);
    mapped.mstart = mstart;
    mapped.nchunks = to_map;
    return 0;
}

template<typename W>
int dax_manager<W>::unmap(chunk nchunks, mapping_part& unmapped, bool deep)
{
    // Determine necessary changes
    chunk to_unmap = 0;
    auto erase_first {_mappings.cend()};
    chunk to_unmap_from_last = 0;
    for (auto it {_mappings.crbegin()};
        to_unmap < nchunks && it != _mappings.crend(); it++) {

        if (it->nchunks <= nchunks - to_unmap) {
            // Remove *it
            erase_first = it.base() - 1;
            to_unmap += it->nchunks;
        } else {
            // Modify *it
            to_unmap_from_last = nchunks - to_unmap;
            to_unmap = nchunks;
        }
    }
    if (to_unmap == 0) {
        // The window is empty, or nchunks is 0
        unmapped.mstart = first_empty();
        unmapped.nchunks = 0;
        return (nchunks == 0) ? 0 : ENODATA;
    }

    // Apply changes
    if (deep) {
        auto mstart = first_empty() - to_unmap;
        auto error = _window.unmap(to_unmap * _chunk_size,
            mstart * _chunk_size);
        if (error) {
            return error;
        }
    }
    _mappings.erase(erase_first, _mappings.cend());
    if (to_unmap_from_last > 0) {
        _mappings.back().nchunks -= to_unmap_from_last;
    }

    unmapped.mstart = first_empty();
    unmapped.nchunks = to_unmap;
    return 0;
}

int dax_window_impl::map(uint64_t nodeid, uint64_t fh, uint64_t len,
    uint64_t fstart, uint64_t mstart)
{
    assert(mstart + len <= _window->len);

    // NOTE: There are restrictions on the arguments to FUSE_SETUPMAPPING, from
    // the spec: "Alignment constraints for FUSE_SETUPMAPPING and
    // FUSE_REMOVEMAPPING requests are communicated during FUSE_INIT
    // negotiation"):
    // - foffset: multiple of map_alignment from FUSE_INIT
    // - len: not larger than remaining file?
    // - moffset: multiple of map_alignment from FUSE_INIT
    // In practice, map_alignment is the host's page size, because foffset and
    // moffset are passed to mmap() on the host. These are satisfied by
    // the caller (chunk size being a multiple of map alignment in dax_manager).

    std::unique_ptr<fuse_setupmapping_in> in_args {
        new (std::nothrow) fuse_setupmapping_in()};
    if (!in_args) {
        return ENOMEM;
    }
    in_args->fh = fh;
    in_args->foffset = fstart;
    in_args->len = len;
    in_args->flags = 0; // Read-only
    in_args->moffset = mstart;

    virtiofs_debug("inode %lld, setting up mapping (foffset=%lld, len=%lld, "
                   "moffset=%lld)\n", nodeid, in_args->foffset, in_args->len,
                   in_args->moffset);
    auto error = fuse_req_send_and_receive_reply(&_drv, FUSE_SETUPMAPPING,
        nodeid, in_args.get(), sizeof(*in_args), nullptr, 0).second;
    if (error) {
        kprintf("[virtiofs] inode %lld, mapping setup failed\n", nodeid);
        return error;
    }

    return 0;
}

int dax_window_impl::unmap(uint64_t len, uint64_t mstart)
{
    assert(mstart + len <= _window->len);

    // NOTE: FUSE_REMOVEMAPPING accepts a fuse_removemapping_in followed by
    // fuse_removemapping_in.count fuse_removemapping_one arguments in general.
    auto in_args_size = sizeof(fuse_removemapping_in) +
        sizeof(fuse_removemapping_one);
    std::unique_ptr<u8> in_args {new (std::nothrow) u8[in_args_size]};
    if (!in_args) {
        return ENOMEM;
    }
    auto r_in = new (in_args.get()) fuse_removemapping_in();
    auto r_one = new (in_args.get() + sizeof(fuse_removemapping_in))
        fuse_removemapping_one();
    r_in->count = 1;
    r_one->moffset = mstart;
    r_one->len = len;

    // The nodeid is irrelevant for the current implementation of
    // FUSE_REMOVEMAPPING. If it needed to be set, would we need to make a
    // request per inode?
    uint64_t nodeid = 0;

    virtiofs_debug("inode %lld, removing mapping (moffset=%lld, len=%lld)\n",
        nodeid, r_one->moffset, r_one->len);
    auto error = fuse_req_send_and_receive_reply(&_drv, FUSE_REMOVEMAPPING,
        nodeid, in_args.get(), in_args_size, nullptr, 0).second;
    if (error) {
        kprintf("[virtiofs] inode %lld, mapping removal failed\n", nodeid);
        return error;
    }

    return 0;
}

template<typename W>
bool dax_manager<W>::find(uint64_t nodeid, chunk fstart, mapping_part& found)
    const
{
    for (auto& m : _mappings) {
        if (m.nodeid == nodeid &&
            m.fstart <= fstart &&
            m.fstart + m.nchunks > fstart) {

            // m contains fstart
            auto excess = fstart - m.fstart; // excess contained in m
            found.nchunks = m.nchunks - excess;
            found.mstart = m.mstart + excess;
            return true;
        }
    }
    return false;
}

template<typename W>
typename dax_manager<W>::chunk dax_manager<W>::first_empty() const
{
    if (_mappings.empty()) {
        return 0;
    }
    auto& m {_mappings.back()};
    return m.mstart + m.nchunks;
}

// Explicitly instantiate the only uses of dax_manager.
template class dax_manager<dax_window_impl>;
template class dax_manager<dax_window_stub>;

}
