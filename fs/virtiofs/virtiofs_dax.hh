/*
 * Copyright (C) 2020 Fotis Xenakis
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <vector>

#include <api/assert.h>
#include <osv/mutex.h>
#include <osv/uio.h>

#include "drivers/virtio-fs.hh"
#include "virtiofs.hh"

namespace virtiofs {

// A manager for the DAX window of a virtio-fs device. This implements a
// straight-forward scheme for file mappings:
// - The window is split into equally-sized chunks. Each mapping occupies an
//   integer amount of consecutive chunks.
// - New mappings are placed on the lowest available chunks in the window.
// - When there are not enough chunks available for a new mapping, the highest
//   (i.e. most recently mapped) chunks occupied are evicted. Thus, chunks are
//   mapped in a LIFO manner (the window resembles a stack).
class dax_manager {
public:
    static constexpr size_t DEFAULT_CHUNK_SIZE = 1 << 21; // 2MiB

    // Construct a new manager for the DAX window associated with @drv (as
    // returned by drv.get_dax()). The alignment constraint of the device (as
    // reported by drv.get_map_alignment()) should be compatible with
    // @chunk_size.
    dax_manager(virtio::fs& drv, size_t chunk_size = DEFAULT_CHUNK_SIZE)
        : _drv {drv},
          _window {drv.get_dax()},
          _chunk_size {chunk_size},
          _window_chunks {_window->len / _chunk_size} {

        assert(_chunk_size % (1ull << _drv.get_map_alignment()) == 0);

        // NOTE: If _window->len % CHUNK_SIZE > 0, that remainder (< CHUNK_SIZE)
        // is effectively ignored.
    }

    // Read @read_amt bytes from @inode, using the DAX window. If @aggressive,
    // try to prefetch as much of the rest of the file as possible.
    int read(virtiofs_inode& inode, uint64_t file_handle, u64 read_amt,
        struct uio& uio, bool aggressive = false);

private:
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
    // Map @nchunks chunks of the file with @nodeid (opened as @fh), starting at
    // chunk @fstart of the file and chunk @mstart of the window. Returns
    // non-zero on failure. Called with _lock held (for writing).
    int map_ll(uint64_t nodeid, uint64_t fh, chunk nchunks, chunk fstart,
        chunk mstart);
    // Unmap @nchunks chunks, starting at chunk @mstart of the window. Returns
    // non-zero on failure. Called with _lock held (for writing).
    int unmap_ll(chunk nchunks, chunk mstart);

    // Return in @found the largest contiguous existing mapping for @nodeid
    // starting at @fstart. If none found, returns false. Called with _lock held
    // (for reading).
    bool find(uint64_t nodeid, chunk fstart, mapping_part& found) const;
    // Returns the first empty chunk in the window, or one-past-the-last if the
    // window is full. Called with _lock held (for reading).
    chunk first_empty() const;

    virtio::fs& _drv;
    const virtio::fs::dax_window* const _window;
    const size_t _chunk_size;
    const chunk _window_chunks;
    // TODO OPT: Switch to rwlock
    mutex _lock;
    std::vector<mapping> _mappings;
};

}
