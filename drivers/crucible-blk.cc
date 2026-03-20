/*
 * Crucible distributed block device driver for OSv
 *
 * Copyright (C) 2024 Waldemar Kozaczuk
 * Copyright (C) 2024 OSv Contributors
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * Implementation: Pure C++ (no Rust dependencies)
 * Protocol: v13 compatible with Oxide Crucible downstairs
 * Features: Triple replication, snapshots, DISCARD, pipelining
 *
 * Note: Includes custom C++ implementation of Rust's bincode serialization.
 */

#include "crucible-blk.hh"
#include "crucible-client.hh"
#include "crucible-types.hh"
#include "blk-common.hh"

#include <osv/device.h>
#include <osv/bio.h>
#include <osv/debug.h>
#include <osv/options.hh>
#include <osv/sched.hh>
#include <osv/mutex.h>
#include <osv/condvar.h>

#include <string>
#include <vector>
#include <deque>
#include <memory>
#include <cstring>
#include <cctype>
#include <algorithm>
#include <errno.h>

using namespace crucible;

/*
 * Asynchronous I/O dispatcher for the Crucible device.
 *
 * The Crucible upstairs client is synchronous: read_sync/write_sync/flush_sync
 * block the calling thread until 2/3 downstairs acknowledge.  We must NOT run
 * that blocking work (nor the biodone it triggers) directly in the strategy
 * callback, because ZFS drives the strategy from a zio I/O taskq thread whose
 * taskq entry (struct ostask) is embedded in the zio itself.  Completing the
 * bio inline calls vdev_disk_bio_done -> zio_interrupt, which re-enqueues that
 * same embedded ostask onto the ZIO_TASKQ_INTERRUPT taskq while the issuing
 * thread is still inside taskqueue_run_locked() running that very entry.  A
 * second taskq thread then runs the zio to completion and frees it, and the
 * issuing thread returns to touch the freed-and-reused ostask -- a use-after-
 * free that corrupts the taskqueue's TAILQ links and crashes later in
 * taskqueue_enqueue/taskqueue_thread_loop.  Virtio and NVMe never hit this
 * because their strategy is async: it queues the request and returns, and the
 * bio is completed later on a dedicated completion thread.  We follow the same
 * model here -- the strategy enqueues the bio and a pool of worker threads
 * performs the blocking I/O and calls biodone off the zio-issue thread.
 */
class crucible_io_dispatcher {
public:
    explicit crucible_io_dispatcher(int nworkers) {
        _running = true;
        _workers.reserve(nworkers);
        for (int i = 0; i < nworkers; i++) {
            auto* t = sched::thread::make([this] { this->worker_loop(); });
            t->start();
            _workers.push_back(t);
        }
    }

    ~crucible_io_dispatcher() {
        WITH_LOCK(_mtx) {
            _running = false;
            _cv.wake_all();
        }
        for (auto* t : _workers) {
            t->join();
            delete t;
        }
    }

    void submit(struct bio* bio) {
        WITH_LOCK(_mtx) {
            _queue.push_back(bio);
            _cv.wake_one();
        }
    }

private:
    void worker_loop() {
        while (true) {
            struct bio* bio = nullptr;
            WITH_LOCK(_mtx) {
                while (_running && _queue.empty()) {
                    _cv.wait(&_mtx);
                }
                if (!_running && _queue.empty()) {
                    return;
                }
                bio = _queue.front();
                _queue.pop_front();
            }
            execute(bio);
        }
    }

    static void execute(struct bio* bio);

    mutex _mtx;
    condvar _cv;
    std::deque<struct bio*> _queue;
    std::vector<sched::thread*> _workers;
    bool _running{false};
};

/**
 * Private data for Crucible block device.
 */
struct crucible_priv {
    std::unique_ptr<UpsairsClient> client;
    std::unique_ptr<crucible_io_dispatcher> dispatcher;
    uint64_t disk_size;
    uint32_t block_size;
    bool read_only;
};

static int crucible_instance = 0;

// Maximum number of Crucible devices supported
#define MAX_CRUCIBLE_DEVICES 8

/**
 * Parse UUID string in format: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
 *
 * @param uuid_str UUID string to parse
 * @param uuid Output UUID structure
 * @return true on success, false on parse error
 */
static bool parse_uuid_string(const std::string& uuid_str, Uuid& uuid)
{
    if (uuid_str.length() != 36) {
        return false;
    }

    // Expected format: 8-4-4-4-12 hex digits with hyphens
    if (uuid_str[8] != '-' || uuid_str[13] != '-' ||
        uuid_str[18] != '-' || uuid_str[23] != '-') {
        return false;
    }

    // Parse hex bytes
    int byte_idx = 0;
    for (size_t i = 0; i < uuid_str.length() && byte_idx < 16; i++) {
        char c = uuid_str[i];
        if (c == '-') {
            continue;
        }
        if (!std::isxdigit(c)) {
            return false;
        }

        // Parse two hex digits into one byte
        if (i + 1 >= uuid_str.length() || uuid_str[i + 1] == '-') {
            return false;
        }

        char hex[3] = {c, uuid_str[i + 1], '\0'};
        uuid.bytes[byte_idx++] = static_cast<uint8_t>(std::strtoul(hex, nullptr, 16));
        i++;
    }

    return byte_idx == 16;
}

/**
 * Parse target list: host1:port1,host2:port2,host3:port3
 *
 * @param targets_str Comma-separated target list
 * @return Vector of target strings
 */
static std::vector<std::string> parse_targets(const std::string& targets_str)
{
    std::vector<std::string> targets;
    size_t start = 0;
    size_t end = 0;

    while ((end = targets_str.find(',', start)) != std::string::npos) {
        if (end > start) {
            targets.push_back(targets_str.substr(start, end - start));
        }
        start = end + 1;
    }

    // Add last target
    if (start < targets_str.length()) {
        targets.push_back(targets_str.substr(start));
    }

    return targets;
}

/*
 * Read/write operations.
 *
 * We MUST go through physio() rather than bdev_read/bdev_write — those
 * route through the OSv buf cache which is hard-coded to BSIZE=512 byte
 * blocks (see fs/vfs/vfs_bdev.cc).  Crucible volumes use 4096-byte (or
 * larger) blocks and read_sync/write_sync reject sub-block requests as
 * EINVAL, so the buf-cache path produces a flood of EIO.  physio() hands
 * the whole uio to crucible_strategy() as a single bio, which is what we
 * actually want anyway: one upstairs round trip per logical I/O.
 */
static int crucible_read(struct device *dev, struct uio *uio, int ioflags)
{
    return physio(dev, uio, ioflags);
}

static int crucible_write(struct device *dev, struct uio *uio, int ioflags)
{
    auto* prv = static_cast<struct crucible_priv*>(dev->private_data);

    if (prv->read_only) {
        return EROFS;
    }

    return physio(dev, uio, ioflags);
}

/**
 * Ioctl operation for Crucible-specific commands.
 */
static int crucible_ioctl(struct device *dev, u_long cmd, void *arg)
{
    auto* prv = static_cast<struct crucible_priv*>(dev->private_data);

    if (!prv || !prv->client) {
        return ENODEV;
    }

    switch (cmd) {
        case CRUCIBLE_IOC_CREATE_SNAPSHOT: {
            if (prv->read_only) {
                return EROFS;
            }

            // Snapshot ID is passed as argument
            uint64_t snapshot_id = *static_cast<uint64_t*>(arg);

            kprintf("[Crucible] Creating snapshot with ID %lu\n", snapshot_id);
            int result = prv->client->create_snapshot(snapshot_id);

            if (result == 0) {
                kprintf("[Crucible] Snapshot %lu created successfully\n", snapshot_id);
            } else {
                kprintf("[Crucible] Snapshot %lu creation failed with error %d\n",
                        snapshot_id, result);
            }

            return result;
        }

        default:
            // Fall back to generic block device ioctl
            return blk_ioctl(dev, cmd, arg);
    }
}

/*
 * Perform the (blocking) I/O for one bio and complete it.  Runs on a
 * dispatcher worker thread, never on the ZFS zio-issue taskq thread.
 */
void crucible_io_dispatcher::execute(struct bio *bio)
{
    auto* prv = static_cast<struct crucible_priv*>(bio->bio_dev->private_data);
    int error = 0;

    if (!prv || !prv->client) {
        biodone(bio, false);
        return;
    }

    try {
        switch (bio->bio_cmd) {
            case BIO_READ:
                error = prv->client->read_sync(
                    bio->bio_offset,
                    bio->bio_bcount,
                    bio->bio_data
                );
                break;

            case BIO_WRITE:
                if (prv->read_only) {
                    error = EROFS;
                } else {
                    error = prv->client->write_sync(
                        bio->bio_offset,
                        bio->bio_bcount,
                        bio->bio_data
                    );
                }
                break;

            case BIO_FLUSH:
                error = prv->client->flush_sync();
                break;

            case BIO_DISCARD:
                if (prv->read_only) {
                    error = EROFS;
                } else {
                    error = prv->client->discard_sync(
                        bio->bio_offset,
                        bio->bio_bcount
                    );
                }
                break;

            default:
                error = ENOTBLK;
                break;
        }
    } catch (const std::exception& e) {
        kprintf("crucible_strategy: exception: %s\n", e.what());
        error = EIO;
    } catch (...) {
        kprintf("crucible_strategy: unknown exception\n");
        error = EIO;
    }

    bio->bio_error = error;
    biodone(bio, error == 0);
}

/**
 * Block I/O strategy function.
 *
 * Hands the bio to the dispatcher's worker pool and returns immediately so the
 * blocking upstairs I/O (and the biodone it triggers) never runs on the ZFS
 * zio-issue taskq thread.  See crucible_io_dispatcher for why this matters.
 */
static void crucible_strategy(struct bio *bio)
{
    auto* prv = static_cast<struct crucible_priv*>(bio->bio_dev->private_data);

    if (!prv || !prv->dispatcher) {
        biodone(bio, false);
        return;
    }

    prv->dispatcher->submit(bio);
}

/**
 * Device operations structure.
 */
static struct devops crucible_devops = {
    .open = no_open,
    .close = no_close,
    .read = crucible_read,
    .write = crucible_write,
    .ioctl = crucible_ioctl,
    .devctl = no_devctl,
    .strategy = crucible_strategy,
};

/**
 * Driver structure.
 */
static struct driver crucible_driver = {
    .name = "crucible",
    .devops = &crucible_devops,
    .devsz = sizeof(struct crucible_priv),
};

namespace crucible {

/**
 * Initialize Crucible block device driver.
 */
int crucible_init(const std::string& targets_str, const std::string& uuid_str,
                  uint32_t block_size, bool read_only, int device_index)
{
    if (targets_str.empty() || uuid_str.empty()) {
        kprintf("crucible_init: missing required options (--crucible%d and --crucible%d-uuid)\n",
                device_index, device_index);
        return EINVAL;
    }

    if (device_index < 0 || device_index >= MAX_CRUCIBLE_DEVICES) {
        kprintf("crucible_init: invalid device_index %d (must be 0-%d)\n",
                device_index, MAX_CRUCIBLE_DEVICES - 1);
        return EINVAL;
    }

    kprintf("crucible_init: Initializing Crucible block device %d\n", device_index);
    kprintf("crucible_init: targets=%s, uuid=%s, block_size=%u, read_only=%d\n",
            targets_str.c_str(), uuid_str.c_str(), block_size, read_only);

    // Parse targets
    auto targets = parse_targets(targets_str);
    if (targets.size() != 3) {
        kprintf("crucible_init: expected 3 targets, got %zu\n", targets.size());
        return EINVAL;
    }

    // Parse UUID
    Uuid region_uuid;
    if (!parse_uuid_string(uuid_str, region_uuid)) {
        kprintf("crucible_init: invalid UUID format: %s\n", uuid_str.c_str());
        return EINVAL;
    }

    // Create Crucible client
    try {
        // For now, use a default total_blocks value
        // This should be queried from the downstairs servers in a real implementation
        uint64_t total_blocks = 0;  // 0 means query from server

        std::unique_ptr<UpsairsClient> client;
        client.reset(new UpsairsClient(
            targets,
            region_uuid,
            block_size,
            total_blocks,
            read_only,
            false  // encrypted - not supported yet
        ));

        // Connect to downstairs servers (non-blocking with exception handling)
        kprintf("crucible_init: attempting to connect to downstairs servers...\n");
        try {
            client->connect();
        } catch (const std::exception& e) {
            kprintf("crucible_init: WARNING: connection failed: %s\n", e.what());
            kprintf("crucible_init: boot will continue, but /dev/crucible%d will not be available\n",
                    device_index);
            return ENOTCONN;
        }

        if (!client->is_connected()) {
            kprintf("crucible_init: WARNING: failed to establish quorum with downstairs servers\n");
            kprintf("crucible_init: boot will continue, but /dev/crucible%d will not be available\n",
                    device_index);
            return ENOTCONN;
        }

        // Create device with specified index
        std::string dev_name = "crucible" + std::to_string(device_index);
        struct device* dev = device_create(&crucible_driver, dev_name.c_str(), D_BLK);
        crucible_instance = std::max(crucible_instance, device_index + 1);

        auto* prv = static_cast<struct crucible_priv*>(dev->private_data);
        prv->client = std::move(client);
        prv->block_size = block_size;
        prv->disk_size = prv->client->total_size();
        prv->read_only = read_only;

        /*
         * Pool of worker threads that run the synchronous upstairs I/O off the
         * caller's (ZFS zio-issue taskq) thread.  ZFS issues many concurrent
         * Writes per txg (the WRITE issue taskq alone has several threads), so
         * a too-small pool would serialize them and throttle txg sync; 16
         * matches the upstairs request fan-out without unbounded thread growth.
         */
        prv->dispatcher.reset(new crucible_io_dispatcher(16));

        dev->size = prv->disk_size;
        dev->block_size = block_size;    // 4096 (or larger); ZFS derives ashift from this
        dev->max_io_size = 1024 * 1024;  // 1MB max I/O

        // Detect partitions
        read_partition_table(dev);

        kprintf("crucible_init: SUCCESS: created device %s, size=%llu bytes, block_size=%u\n",
                dev_name.c_str(), prv->disk_size, prv->block_size);

    } catch (const std::exception& e) {
        kprintf("crucible_init: WARNING: failed to create client: %s\n", e.what());
        kprintf("crucible_init: boot will continue, but /dev/crucible%d will not be available\n",
                device_index);
        return EIO;
    } catch (...) {
        kprintf("crucible_init: WARNING: unknown exception during initialization\n");
        kprintf("crucible_init: boot will continue, but /dev/crucible%d will not be available\n",
                device_index);
        return EIO;
    }

    return 0;
}

} // namespace crucible
