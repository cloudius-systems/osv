/*
 * Copyright (C) 2020 Fotis Xenakis
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef VIRTIOFS_DAX_HH
#define VIRTIOFS_DAX_HH

#include <vector>

#include <api/assert.h>
#include <osv/mutex.h>
#include <osv/uio.h>

#include "drivers/virtio-fs.hh"

// Necessary pre-declaration because virtiofs_inode is declared in virtiofs.hh,
// which depends on this for defining virtiofs_mount_data.
struct virtiofs_inode;

namespace virtiofs {

// A thin abstraction layer over the actual DAX window, taking care of all the
// low-level operations, interfacing with the driver.
class dax_window_impl {
public:
    // Construct a new window for the DAX window associated with @drv (as
    // returned by drv.get_dax()).
    dax_window_impl(virtio::fs& drv)
        : _drv {drv},
          _window {drv.get_dax()} {}

    // Returns the size of the window in bytes.
    u64 size() const { return _window->len; }
    // Returns a pointer to the underlying memory region.
    mmioaddr_t data() const { return _window->addr; }
    // Returns the map alignment for the window.
    int map_alignment() const { return _drv.get_map_alignment(); }
    // Map @len bytes of the file with @nodeid (opened as @fh), starting at
    // byte @fstart of the file and byte @mstart of the window. Returns
    // non-zero on failure.
    int map(uint64_t nodeid, uint64_t fh, uint64_t len, uint64_t fstart,
        uint64_t mstart);
    // Unmap @len bytes, starting at byte @mstart of the window. Returns
    // non-zero on failure.
    int unmap(uint64_t len, uint64_t mstart);

private:
    virtio::fs& _drv;
    const virtio::fs::dax_window* const _window;
};

// A stub DAX window, used for testing. Defined here to facilitate instantiation
// of dax_manager<dax_window_stub> (see virtiofs_dax.cc).
class dax_window_stub {
public:
    dax_window_stub(u64 len)
        : _len {len} {}

    u64 size() const { return _len; }
    mmioaddr_t data() const { return nullptr; }
    int map_alignment() const { return 12; /* 4KiB */ }
    int map(uint64_t nodeid, uint64_t fh, uint64_t len, uint64_t fstart,
        uint64_t mstart) { return 0; };
    int unmap(uint64_t len, uint64_t mstart)  { return 0; };

private:
    u64 _len;
};

// A manager for the DAX window of a virtio-fs device. This implements a
// straight-forward scheme for file mappings:
// - The window is split into equally-sized chunks. Each mapping occupies an
//   integer amount of consecutive chunks.
// - New mappings are placed on the lowest available chunks in the window.
// - When there are not enough chunks available for a new mapping, the highest
//   (i.e. most recently mapped) chunks occupied are evicted. Thus, chunks are
//   mapped in a LIFO manner (the window resembles a stack).
template<typename W>
class dax_manager {
public:
    static constexpr size_t DEFAULT_CHUNK_SIZE = 1 << 21; // 2MiB

    // Construct a new manager for @window. The @chunk_size should be compatible
    // with the alignment constraint of @window.
    dax_manager(W window, size_t chunk_size = DEFAULT_CHUNK_SIZE)
        : _window {window},
          _chunk_size {chunk_size},
          _window_chunks {_window.size() / _chunk_size} {

        assert(_chunk_size % (1ull << _window.map_alignment()) == 0);

        // NOTE: If _window->len % CHUNK_SIZE > 0, that remainder (< CHUNK_SIZE)
        // is effectively ignored.
    }

    // Read @read_amt bytes from @inode, using the DAX window. If @aggressive,
    // try to prefetch as much of the rest of the file as possible.
    int read(virtiofs_inode& inode, uint64_t file_handle, u64 read_amt,
        struct uio& uio, bool aggressive = false);

protected:
    // Helper type to better distinguish referring to chunks vs bytes
    using chunk = size_t;

    struct mapping {
        mapping(uint64_t _nodeid, chunk _nchunks, chunk _fstart, chunk _mstart)
            : nodeid {_nodeid},
              nchunks {_nchunks},
              fstart {_fstart},
              mstart {_mstart} {}
        uint64_t nodeid;
        chunk nchunks;
        chunk fstart;
        chunk mstart;
    };

    struct mapping_part {
        chunk nchunks;
        chunk mstart;
    };

    // Map up to @nchunks chunks of the file with @nodeid, starting at chunk
    // @fstart of the file, after all other mappings. If @evict, evict other
    // chunks if necessary. Returns in @mapped the new mapping and non-zero on
    // failure. Called with _lock held (for writing).
    int map(uint64_t nodeid, uint64_t file_handle, chunk nchunks, chunk fstart,
        mapping_part& mapped, bool evict = false);
    // Unmap @nchunks last chunks, also doing an actual unmapping on the device
    // if @deep. Returns in @unmapped what was unmapped and non-zero on failure.
    // Called with _lock held (for writing).
    int unmap(chunk nchunks, mapping_part& unmapped, bool deep = false);

    // Return in @found the largest contiguous existing mapping for @nodeid
    // starting at @fstart. If none found, returns false. Called with _lock held
    // (for reading).
    bool find(uint64_t nodeid, chunk fstart, mapping_part& found) const;
    // Returns the first empty chunk in the window, or one-past-the-last if the
    // window is full. Called with _lock held (for reading).
    chunk first_empty() const;

    W _window;
    const size_t _chunk_size;
    const chunk _window_chunks;
    // TODO OPT: Switch to rwlock
    mutex _lock;
    std::vector<mapping> _mappings;
};

using dax_manager_impl = dax_manager<dax_window_impl>;
using dax_manager_stub = dax_manager<dax_window_stub>;

}

#endif
