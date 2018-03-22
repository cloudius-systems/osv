/*
 * Copyright (C) 2017 Waldemar Kozaczuk
 * Inspired by original MFS implementation by James Root from 2015
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "rofs.hh"
#include <list>
#include <unordered_map>
#include <include/osv/uio.h>
#include <osv/debug.h>
#include <osv/sched.hh>

/*
 * From cache perspective let us divide each file into sequence of contiguous 32K segments.
 * The files smaller or equal than 32K get loaded in one read, others get loaded
 * segment by segment.
 **/
//
//TODO These 2 values can be made configurable
#define CACHE_SEGMENT_SIZE_IN_BLOCKS 64  // 32K
#define CACHE_SEGMENT_INDEX(offset) (offset >> 15)

#if defined(ROFS_DIAGNOSTICS_ENABLED)
extern std::atomic<long> rofs_block_allocated;
extern std::atomic<long> rofs_cache_reads;
extern std::atomic<long> rofs_cache_misses;
#endif

namespace rofs {
//
// This structure holds cache information and data of specific file
struct file_cache {
    std::unordered_map<uint64_t, struct file_cache_segment *> segments_by_index;
    struct rofs_inode *inode;
    struct rofs_super_block *sb;
};

//
// This structure holds block_count (typically CACHE_SEGMENT_SIZE_IN_BLOCKS) of 512 blocks
// of file data starting at starting_block * 512 byte offset relative to the beginning
// of the file.
class file_cache_segment {
private:
    struct file_cache *cache; // Parent file cache
    void *data;               // Copy of data on disk
    uint64_t starting_block;  // This is relative to the 512-block of the inode itself
    uint64_t block_count;     // Length of data in 512 blocks
    bool data_ready;          // Has data been fully read from disk?

public:
    file_cache_segment(struct file_cache *_cache, uint64_t _starting_block, uint64_t _block_count) {
        this->cache = _cache;
        this->starting_block = _starting_block;
        this->block_count = _block_count;
        this->data_ready = false;   // Data has to be loaded from disk
        this->data = malloc(_cache->sb->block_size * _block_count);
#if defined(ROFS_DIAGNOSTICS_ENABLED)
        rofs_block_allocated += block_count;
#endif
    }

    ~file_cache_segment() {
        free(this->data);
    }

    uint64_t length() {
        return this->block_count * this->cache->sb->block_size;
    }

    bool is_data_ready() {
        return this->data_ready;
    }

    //
    // Read data from memory per uio
    int read(struct uio *uio, uint64_t offset_in_segment, uint64_t bytes_to_read) {
        print("[rofs] [%d] -> file_cache_segment::read() i-node: %d, starting block %d, reading [%d] bytes at segment offset [%d]\n",
              sched::thread::current()->id(), cache->inode->inode_no, starting_block, bytes_to_read,
              offset_in_segment);
        return uiomove(data + offset_in_segment, bytes_to_read, uio);
    }

    //
    // Read all segment data from disk and copy to memory
    int read_from_disk(struct device *device) {
        auto block = cache->inode->data_offset + starting_block;
        auto bytes_remaining = cache->inode->file_size - starting_block * cache->sb->block_size;
        auto blocks_remaining = bytes_remaining / cache->sb->block_size;
        if (bytes_remaining % cache->sb->block_size > 0) {
            blocks_remaining++;
        }
        auto block_count_to_read = std::min(block_count, blocks_remaining);
        print("[rofs] [%d] -> file_cache_segment::write() i-node: %d, starting block %d, reading [%d] blocks at disk offset [%d]\n",
              sched::thread::current()->id(), cache->inode->inode_no, starting_block, block_count_to_read, block);
        auto error = rofs_read_blocks(device, block, block_count_to_read, data);
        this->data_ready = (error == 0);
        if (error) {
            print("!!!!! Error reading from disk\n");
        }
        return error;
    }
};

static std::unordered_map<uint64_t, struct file_cache *> file_cache_by_node_id;
static mutex file_cache_lock;

static struct file_cache *get_or_create_file_cache(struct rofs_inode *inode, struct rofs_super_block *sb) {
    // This is the only global mutex
    WITH_LOCK(file_cache_lock) {
        auto cache_entry = file_cache_by_node_id.find(inode->inode_no);
        if (cache_entry == file_cache_by_node_id.end()) {
            struct file_cache *new_cache = new file_cache();
            new_cache->inode = inode;
            new_cache->sb = sb;
            file_cache_by_node_id.emplace(inode->inode_no, new_cache);
            return new_cache;
        } else {
            return cache_entry->second;
        }
    }
}

enum CacheTransactionType {
    READ_FROM_MEMORY = 1,
    READ_FROM_DISK
};

// This represents an operation/transaction to read data from segment memory or/and from disk
struct cache_segment_transaction {
    struct file_cache_segment *segment;
    CacheTransactionType transaction_type;
    uint64_t segment_offset;
    uint64_t bytes_to_read;

    cache_segment_transaction(file_cache_segment *_segment, uint64_t file_offset, uint64_t _bytes_to_read) {
        this->segment = _segment;
        if (_segment->is_data_ready()) {
            this->transaction_type = CacheTransactionType::READ_FROM_MEMORY;
        } else {
            this->transaction_type = CacheTransactionType::READ_FROM_DISK;
        }
        this->segment_offset = file_offset % segment->length();
        this->bytes_to_read = std::min(segment->length() - segment_offset, _bytes_to_read);
    }
};

//
// This function analyzes uio against existing segments in file_cache
// and builds a vector of transactions/operation that is used by cache_read to tell it
// to either read data from memory in cache segment or read data from disk into
// new segment
static std::vector<struct cache_segment_transaction>
plan_cache_transactions(struct file_cache *cache, struct uio *uio) {

    std::vector<struct cache_segment_transaction> transactions;
    //
    // Check if file is small enough to fit into cache segment
    if (cache->segments_by_index.empty() &&
        cache->inode->file_size <= (CACHE_SEGMENT_SIZE_IN_BLOCKS * cache->sb->block_size)) {
        auto block_count = cache->inode->file_size / cache->sb->block_size;
        if (cache->inode->file_size % cache->sb->block_size > 0) {
            block_count++;
        }
        auto new_cache_segment = new file_cache_segment(cache, 0, block_count);
        cache->segments_by_index.emplace(0, new_cache_segment);
        uint64_t read_amt = std::min<uint64_t>(cache->inode->file_size - uio->uio_offset, uio->uio_resid);
        transactions.push_back(cache_segment_transaction(new_cache_segment, uio->uio_offset, read_amt));
        print("[rofs] [%d] -> rofs_cache_get_segment_operations i-node: %d, read FULL file of %d bytes\n",
              sched::thread::current()->id(), cache->inode->inode_no, uio->uio_resid);
        return transactions;
    }
    //
    // File is larger than cache segment or previous attempt to read from disk failed
    uint64_t file_offset = uio->uio_offset;
    uint64_t bytes_to_read = std::min<uint64_t>(cache->inode->file_size - uio->uio_offset, uio->uio_resid);
    while (bytes_to_read > 0) {
        //
        // Next try to see if any cache segment is hit
        auto cache_segment_index = CACHE_SEGMENT_INDEX(file_offset);
        auto cache_segment = cache->segments_by_index.find(cache_segment_index);
        if (cache_segment != cache->segments_by_index.end()) {
            print("[rofs] [%d] -> rofs_cache_get_segment_operations i-node: %d, cache segment %d HIT at file offset %d\n",
                  sched::thread::current()->id(), cache->inode->inode_no, cache_segment_index, file_offset);

            auto transaction = cache_segment_transaction(cache_segment->second, file_offset, bytes_to_read);
            file_offset += transaction.bytes_to_read;
            bytes_to_read -= transaction.bytes_to_read;
            transactions.push_back(transaction);
        }
            //
            // Miss -> read from disk
        else {
            print("[rofs] [%d] -> rofs_cache_get_segment_operations i-node: %d, cache segment %d MISS at file offset %d\n",
                  sched::thread::current()->id(), cache->inode->inode_no, cache_segment_index, file_offset);
            uint64_t segment_starting_block = cache_segment_index * CACHE_SEGMENT_SIZE_IN_BLOCKS;
            //
            // Allocate new cache segment
            auto new_cache_segment = new file_cache_segment(cache, segment_starting_block,
                                                            CACHE_SEGMENT_SIZE_IN_BLOCKS);
            cache->segments_by_index.emplace(cache_segment_index, new_cache_segment);

            auto transaction = cache_segment_transaction(new_cache_segment, file_offset, bytes_to_read);
            file_offset += transaction.bytes_to_read;;
            bytes_to_read -= transaction.bytes_to_read;
            transactions.push_back(transaction);
        }
    }

    return transactions;
}

//
// This function calls plan_cache_transactions first to identify what part of uio can be
// read from memory and what needs to be read from disk
// NOTE: This function is NOT thread-safe and does not need to be because it is called only
// by rofs_read_with_cache() which in turn is called by vfs_file::read() in a critical section
// specific to given file. So effectively cache_read() is assumed to be called by one thread
// at a time and no thread synchronization is needed.
int
cache_read(struct rofs_inode *inode, struct device *device, struct rofs_super_block *sb, struct uio *uio) {
    //
    // Find existing one or create new file cache
    struct file_cache *cache = get_or_create_file_cache(inode, sb);

    //
    // Prepare list of cache transactions (copy from memory
    // or read from disk into cache memory and then copy into memory)
    auto segment_transactions = plan_cache_transactions(cache, uio);
    print("[rofs] [%d] rofs_cache_read called for i-node [%d] at %d with %d ops\n",
          sched::thread::current()->id(), inode->inode_no, uio->uio_offset, segment_transactions.size());

    int error = 0;

    // Iterate over the list of cache operation and either copy from memory
    // or read from disk into cache memory and then copy into memory
    auto it = segment_transactions.begin();
    for (; it != segment_transactions.end(); ++it) {
        auto transaction = *it;
#if defined(ROFS_DIAGNOSTICS_ENABLED)
        rofs_cache_reads += 1;
#endif
        if (transaction.transaction_type == CacheTransactionType::READ_FROM_MEMORY) {
            //
            // Copy data from segment to target buffer
            error = transaction.segment->read(uio, transaction.segment_offset, transaction.bytes_to_read);
        }
        // Read from disk into segment missing in cache or empty segment that was in cache but had not data because
        // of failure to read
        else {
            error = transaction.segment->read_from_disk(device);
#if defined(ROFS_DIAGNOSTICS_ENABLED)
            rofs_cache_misses += 1;
#endif
            //
            // Copy data from segment to target buffer
            if (!error) {
                error = transaction.segment->read(uio, transaction.segment_offset, transaction.bytes_to_read);
            }
        }

        if (error) {
            break;
        }
    }

    print("[rofs] [%d] rofs_cache_read completed for i-node [%d]\n", sched::thread::current()->id(),
          inode->inode_no);
    return error;
}

}
