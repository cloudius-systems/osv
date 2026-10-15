/*
 * Copyright (C) 2024 Waldemar Kozaczuk
 * Copyright (C) 2024 OSv Contributors
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "crucible-client.hh"
#include "crucible-messages.hh"
#include "crucible-hash.hh"

#include <osv/sched.hh>
#include <osv/debug.h>

#include <random>
#include <sstream>
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <sys/select.h>
#include <arpa/inet.h>
#include <errno.h>
#include <limits>

// OSv uses kprintf for debug logging
extern "C" {
    int kprintf(const char* fmt, ...);
}

using namespace crucible;

namespace crucible {

// Helper: Generate random UUID
Uuid generate_uuid()
{
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint8_t> dis(0, 255);

    Uuid uuid;
    for (int i = 0; i < 16; i++) {
        uuid.bytes[i] = dis(gen);
    }

    // Set version (4) and variant bits
    uuid.bytes[6] = (uuid.bytes[6] & 0x0F) | 0x40;  // Version 4
    uuid.bytes[8] = (uuid.bytes[8] & 0x3F) | 0x80;  // Variant 10

    return uuid;
}

// Helper: Parse "host:port" string
std::pair<std::string, uint16_t> parse_target_string(const std::string& target)
{
    auto colon = target.rfind(':');
    if (colon == std::string::npos) {
        throw std::runtime_error("Invalid target format (expected host:port): " + target);
    }

    std::string host = target.substr(0, colon);
    std::string port_str = target.substr(colon + 1);

    in_addr address{};
    if (inet_pton(AF_INET, host.c_str(), &address) != 1 || port_str.empty() ||
        port_str.find_first_not_of("0123456789") != std::string::npos) {
        throw ConnectionError("Expected numeric IPv4:decimal-port target");
    }
    auto port = std::stoul(port_str);
    if (!port || port > 65535) {
        throw ConnectionError("Port must be in 1..65535");
    }
    return {host, static_cast<uint16_t>(port)};
}

// UpsairsClient implementation

UpsairsClient::UpsairsClient(const std::vector<std::string>& targets,
                             const Uuid& region_uuid,
                             uint32_t block_size,
                             uint64_t total_blocks,
                             bool read_only,
                             bool encrypted,
                             uint64_t generation)
    : targets_(targets)
    , region_uuid_(region_uuid)
    , upstairs_id_(generate_uuid())
    , session_id_(generate_uuid())
    , block_size_(block_size)
    , total_blocks_(total_blocks)
    , generation_(generation)
    , read_only_(read_only)
    , encrypted_(encrypted)
{
    for (auto& admitted : admitted_) { admitted.store(false); }
    if (!generation_ || encrypted_) {
        throw std::runtime_error("Nonzero external generation and unencrypted region required");
    }

    if (targets.size() != 3) {
        throw std::runtime_error("Crucible requires exactly 3 downstairs targets");
    }

    if (block_size == 0 || (block_size & (block_size - 1)) != 0) {
        throw std::runtime_error("Block size must be power of 2");
    }
}

UpsairsClient::~UpsairsClient()
{
    disconnect();
}

void UpsairsClient::connect()
{
    std::unique_lock<mutex> lifecycle_guard(lifecycle_mtx_);
    if (stopping_) {
        throw ConnectionError("Session stopped");
    }
    if (running_) {
        return;  // Already connected
    }

    if (attempted_) {
        throw std::runtime_error("Session cannot restart: repair and fresh admission required");
    }
    attempted_ = true;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    try {
        // Validate the entire configuration before promoting any replica.
        std::array<std::pair<std::string, uint16_t>, 3> parsed;
        for (size_t i = 0; i < 3; ++i) { parsed[i] = parse_target_string(targets_[i]); }
        for (size_t i = 0; i < 3; i++) {
            const auto& target = parsed[i];
            connections_[i].reset(new Connection(target.first, target.second, stopping_, deadline));
            handshake(i, deadline);
            query_region_info(i, deadline);
        }
        validate_cohort();
        if (stopping_) {
            throw ConnectionError("Admission cancelled");
        }
        for (auto& admitted : admitted_) {
            admitted = true;
        }
        connected_count_ = 3;

        // Start the per-downstairs sender threads, then the I/O (response) thread.
        start_senders();
        running_ = true;
        io_thread_ = sched::thread::make([this] { this->io_loop(); });
        io_thread_->start();

        kprintf("[Crucible] upstairs ready (%d/3 downstairs)\n",
                connected_count_.load());
    } catch (...) {
        stopping_ = true;
        disconnect_locked();
        throw;
    }
}

void UpsairsClient::disconnect()
{
    stopping_ = true; // visible to the admission owner before waiting for it
    std::unique_lock<mutex> lifecycle_guard(lifecycle_mtx_);
    disconnect_locked();
}

void UpsairsClient::disconnect_locked()
{
    running_ = false;
    for (int i = 0; i < 3; ++i) {
        fail_downstairs(i);
        if (connections_[i]) {
            connections_[i]->shutdown();
        }
    }
    request_mgr_.cancel_all();
    stop_senders();
    if (io_thread_) {
        io_thread_->join();
        delete io_thread_;
        io_thread_ = nullptr;
    }
    // Drain the admitted API operation after waking its request.
    std::unique_lock<mutex> operation_guard(operation_mtx_);
    // No descriptor recycling until all socket users have stopped.
    for (auto& conn : connections_) {
        if (conn) {
            conn->close();
        }
    }
}

void UpsairsClient::fail_downstairs(int index)
{
    if (!admitted_[index].exchange(false)) {
        return;
    }
    connected_count_--;
    connections_[index]->shutdown();
    auto& sender = senders_[index];
    WITH_LOCK(sender.mtx) {
        sender.queue.clear();
        sender.queued_bytes = 0;
    }
    request_mgr_.fail_downstairs(index);
    // No replay log or live repair: never re-admit this socket/session.
    kprintf("[Crucible] downstairs %d quarantined until external repair\n", index);
}

void UpsairsClient::start_senders()
{
    for (int i = 0; i < 3; i++) {
        auto& s = senders_[i];
        s.queue.clear();
        s.running = true;
        s.thread = sched::thread::make([this, i] { this->sender_loop(i); });
        s.thread->start();
    }
}

void UpsairsClient::stop_senders()
{
    // Signal each sender to stop and wake any blocked on an empty queue.
    for (int i = 0; i < 3; i++) {
        auto& s = senders_[i];
        WITH_LOCK(s.mtx) {
            s.running = false;
            s.cv.wake_all();
        }
    }

    // A sender wedged in a blocking send() under TCP backpressure will not
    // observe running_ until its socket operation returns.  Close the sockets
    // so that send() fails immediately and the loop exits, instead of blocking
    // join() forever.
    for (auto& conn : connections_) {
        if (conn) {
            conn->shutdown();
        }
    }

    for (int i = 0; i < 3; i++) {
        auto& s = senders_[i];
        if (s.thread) {
            s.thread->join();
            delete s.thread;
            s.thread = nullptr;
        }
        s.queue.clear();
    }
}

bool UpsairsClient::enqueue_frame(int downstairs_idx,
                                  std::vector<uint8_t> header,
                                  std::vector<uint8_t> data)
{
    auto& conn = connections_[downstairs_idx];
    if (!admitted_[downstairs_idx] || !conn) {
        return false;
    }
    auto& s = senders_[downstairs_idx];
    WITH_LOCK(s.mtx) {
        if (!s.running || !admitted_[downstairs_idx]) {
            return false;
        }
        // Includes at most 32 queued frames / 4 MiB plus one active send.
        // A laggard is quarantined, never buffered without a ceiling.
        size_t bytes = header.size() + data.size();
        if (s.queue.size() < 32 && bytes <= 4 * 1024 * 1024 - s.queued_bytes) {
            s.queue.push_back(SendFrame{std::move(header), std::move(data)});
            s.queued_bytes += bytes;
            s.cv.wake_one();
            return true;
        }
    }
    fail_downstairs(downstairs_idx);
    return false;
}

void UpsairsClient::sender_loop(int downstairs_idx)
{
    auto& s = senders_[downstairs_idx];
    while (true) {
        SendFrame frame;
        WITH_LOCK(s.mtx) {
            while (s.running && s.queue.empty()) {
                s.cv.wait(&s.mtx);
            }
            if (!s.running && s.queue.empty()) {
                return;
            }
            frame = std::move(s.queue.front());
            s.queued_bytes -= frame.header.size() + frame.data.size();
            s.queue.pop_front();
        }

        auto& conn = connections_[downstairs_idx];
        if (!admitted_[downstairs_idx] || !conn) {
            // Quarantined socket; drop the frame. The
            // owning request's quorum is satisfied by the other downstairs, or
            // fails via fail_downstairs() / the wait_for_quorum backstop.
            continue;
        }

        try {
            // Header and (optional) data go back to back under the connection
            // send mutex so they reach the wire as one contiguous Crucible
            // frame.  This sender is now the sole writer on the data path for
            // this downstairs, so no other thread can interleave bytes.
            if (frame.data.empty()) {
                conn->send_exact(frame.header.data(), frame.header.size());
            } else {
                conn->send_exact_with_data(
                    frame.header.data(), frame.header.size(),
                    frame.data.data(), frame.data.size());
            }
        } catch (const std::exception& e) {
            // Both directions use the same idempotent quarantine path.
            kprintf("[Crucible] sender %d: send failed: %s\n",
                    downstairs_idx, e.what());
            fail_downstairs(downstairs_idx);
        }
    }
}

bool UpsairsClient::is_connected() const
{
    return running_ && connected_count_ >= 2;
}

int UpsairsClient::read_sync(uint64_t offset, uint32_t length, void* buffer)
{
    // ponytail: serialize jobs; range-aware dependency tracking can restore parallelism.
    std::unique_lock<mutex> operation_guard(operation_mtx_);
    if (!is_connected()) {
        return EIO;
    }

    // Validate parameters
    if (offset > total_size() || length > total_size() - offset) {
        return EINVAL;
    }

    if (length % block_size_ != 0 || offset % block_size_ != 0) {
        return EINVAL;
    }

    if (!length) { return 0; }
    if (!buffer || length > 1024 * 1024) { return EINVAL; }

    try {
        // Calculate block range
        uint64_t start_block = offset / block_size_;
        uint64_t block_count = length / block_size_;
        uint8_t* data_ptr = static_cast<uint8_t*>(buffer);

        // Allocate job ID
        uint64_t job_id;
        auto dependencies = begin_job(job_id);

        // Create pending request
        auto req = request_mgr_.create_request(job_id, MessageType::ReadResponse, length);

        // Build ReadRequest message
        ReadRequest read_msg;
        read_msg.upstairs_id = upstairs_id_;
        read_msg.session_id = session_id_;
        read_msg.job_id = job_id;
        read_msg.dependencies = std::move(dependencies);
        read_msg.start_block = start_block;
        read_msg.count = block_count;

        // Encode message
        auto frame = encode_message(read_msg);

        // Dispatch only to members of the admitted cohort.
        int sent_count = 0;
        for (int i = 0; i < 3; i++) {
            if (enqueue_frame(i, frame)) {
                sent_count++;
            } else {
                req->mark_response(i, false, CrucibleError::ConnectionError);
            }
        }

        if (sent_count < 2) {
            request_mgr_.remove_request(job_id);
            for (int i = 0; i < 3; ++i) { fail_downstairs(i); }
            kprintf("[Crucible] read job_id=%lu: only %d of 3 downstairs reachable\n",
                    job_id, sent_count);
            return EIO;
        }

        if (!req->wait_for_quorum()) {
            request_mgr_.remove_request(job_id);
            for (int i = 0; i < 3; ++i) { fail_downstairs(i); }
            kprintf("[Crucible] read job_id=%lu: quorum not reached\n", job_id);
            return EIO;
        }

        std::unique_lock<mutex> response_guard(req->mtx);

        // Find first successful response with data
        int source_idx = -1;
        for (int i = 0; i < 3; i++) {
            if (req->downstairs_succeeded[i] && !req->read_data[i].empty()) {
                source_idx = i;
                break;
            }
        }

        if (source_idx < 0) {
            response_guard.unlock();
            request_mgr_.remove_request(job_id);
            for (int i = 0; i < 3; ++i) { fail_downstairs(i); }
            kprintf("[Crucible] Read job_id=%lu: no valid data received\n", job_id);
            return EIO;
        }

        // Each credited response was validated in full by the response owner.
        const auto& data = req->read_data[source_idx];
        std::memcpy(data_ptr, data.data(), length);
        response_guard.unlock();
        request_mgr_.remove_request(job_id);
        return 0;
    } catch (...) {
        fence_session();
        request_mgr_.cancel_all();
        throw;
    }
}

int UpsairsClient::write_sync(uint64_t offset, uint32_t length, const void* buffer)
{
    // ponytail: serialize jobs; range-aware dependency tracking can restore parallelism.
    std::unique_lock<mutex> operation_guard(operation_mtx_);
    if (!is_connected()) {
        return EIO;
    }

    if (read_only_) {
        return EROFS;
    }

    // Validate parameters
    if (offset > total_size() || length > total_size() - offset) {
        return EINVAL;
    }

    if (length % block_size_ != 0 || offset % block_size_ != 0) {
        return EINVAL;
    }

    if (!length) { return 0; }
    if (!buffer || length > 1024 * 1024) { return EINVAL; }

    try {
        // Calculate block range
        uint64_t start_block = offset / block_size_;
        uint64_t block_count = length / block_size_;
        const uint8_t* data_ptr = static_cast<const uint8_t*>(buffer);

        // Chain after the preceding operation on every replica.
        uint64_t job_id;
        auto dependencies = begin_job(job_id);

        // Create pending request
        auto req = request_mgr_.create_request(job_id, MessageType::WriteAck);

        // Build block contexts with hashes
        std::vector<BlockContext> contexts;
        contexts.reserve(block_count);

        for (uint64_t i = 0; i < block_count; i++) {
            BlockContext ctx;
            ctx.hash = xxhash64_block(data_ptr + i * block_size_, block_size_);
            ctx.encryption_ctx = nullopt;  // No encryption for now
            contexts.push_back(ctx);
        }

        /*
         * Build the Write message header.  The frame is encoded as:
         *   [u32 LE total_len][header bytes][u64 LE data_len=length][data]
         * The header through the u64 data length prefix is produced by
         * encode_message_with_data_header(); we then send the actual block
         * data immediately afterwards on the same socket.
         */
        Write write_msg;
        write_msg.upstairs_id = upstairs_id_;
        write_msg.session_id = session_id_;
        write_msg.job_id = job_id;
        write_msg.dependencies = std::move(dependencies);
        write_msg.start_block = start_block;
        write_msg.contexts = std::move(contexts);

        auto header_frame = encode_message_with_data_header(write_msg, length);

        /*
         * Enqueue the header+data pair to each connected downstairs sender.
         * The sender thread writes the two halves back to back under the
         * connection send mutex, so they reach the wire as one atomic frame
         * (ZFS's TXG sync issues many concurrent Writes; interleaved bytes on
         * the socket trigger "bytes remaining on stream" / disconnect).  The
         * enqueue itself is non-blocking, so a downstairs that is backpressuring
         * TCP stalls only its own sender -- this write still dispatches to the
         * other two and their 2-of-3 quorum completes it.  This is the fix for
         * the 256-MiB flush-contention hang.
         */
        int sent_count = 0;
        for (int i = 0; i < 3; i++) {
            std::vector<uint8_t> data(data_ptr, data_ptr + length);
            if (enqueue_frame(i, header_frame, std::move(data))) {
                sent_count++;
            } else {
                req->mark_response(i, false, CrucibleError::ConnectionError);
            }
        }

        if (sent_count < 2) {
            request_mgr_.remove_request(job_id);
            for (int i = 0; i < 3; ++i) { fail_downstairs(i); }
            kprintf("[Crucible] write job_id=%lu: only %d of 3 downstairs reachable\n",
                    job_id, sent_count);
            return EIO;
        }

        if (!req->wait_for_quorum()) {
            request_mgr_.remove_request(job_id);
            for (int i = 0; i < 3; ++i) { fail_downstairs(i); }
            kprintf("[Crucible] write job_id=%lu: quorum not reached\n", job_id);
            return EIO;
        }

        request_mgr_.remove_request(job_id);
        return 0;
    } catch (...) {
        fence_session();
        request_mgr_.cancel_all();
        throw;
    }
}

int UpsairsClient::flush_sync()
{
    // ponytail: serialize jobs; range-aware dependency tracking can restore parallelism.
    std::unique_lock<mutex> operation_guard(operation_mtx_);
    if (!is_connected()) {
        return EIO;
    }

    if (read_only_) {
        return 0;  // No-op for read-only
    }

    try {
        // Chain after all prior operations, including reads.
        uint64_t job_id;
        if (flush_number_ == std::numeric_limits<uint64_t>::max()) {
            fence_session();
            return EIO;
        }
        auto dependencies = begin_job(job_id);
        ++flush_number_;

        // Create pending request
        auto req = request_mgr_.create_request(job_id, MessageType::FlushAck);

        // Build Flush message
        Flush flush_msg;
        flush_msg.upstairs_id = upstairs_id_;
        flush_msg.session_id = session_id_;
        flush_msg.job_id = job_id;
        flush_msg.dependencies = std::move(dependencies);
        flush_msg.flush_number = flush_number_;
        flush_msg.gen_number = generation_;
        /*
         * No snapshot, no per-extent flush limit: both fields are encoded as
         * None (single 0 byte each).  Setting extent_limit to extent_count
         * was a leftover that the upstream protocol does not expect.
         */
        flush_msg.snapshot_name = nullopt;
        flush_msg.extent_limit = nullopt;

        auto frame = encode_message(flush_msg);

        /*
         * Enqueue to all connected downstairs (non-blocking).  A flush makes the
         * downstairs fsync every dirty extent, which is the slow operation that
         * backpressures TCP; routing it through the per-downstairs sender queue
         * is exactly what keeps one slow replica from stalling the flush to the
         * other two, so the 2-of-3 quorum still completes.
         */
        int sent_count = 0;
        for (int i = 0; i < 3; i++) {
            if (enqueue_frame(i, frame)) {
                sent_count++;
            } else {
                req->mark_response(i, false, CrucibleError::ConnectionError);
            }
        }

        if (sent_count < 2) {
            request_mgr_.remove_request(job_id);
            for (int i = 0; i < 3; ++i) { fail_downstairs(i); }
            kprintf("[Crucible] flush job_id=%lu: only %d of 3 downstairs reachable\n",
                    job_id, sent_count);
            return EIO;
        }

        if (!req->wait_for_quorum()) {
            request_mgr_.remove_request(job_id);
            for (int i = 0; i < 3; ++i) { fail_downstairs(i); }
            kprintf("[Crucible] flush job_id=%lu: quorum not reached\n", job_id);
            return EIO;
        }

        request_mgr_.remove_request(job_id);
        return 0;
    } catch (...) {
        fence_session();
        request_mgr_.cancel_all();
        throw;
    }
}

int UpsairsClient::create_snapshot(uint64_t snapshot_id)
{
    // ponytail: serialize jobs; range-aware dependency tracking can restore parallelism.
    std::unique_lock<mutex> operation_guard(operation_mtx_);
    if (!is_connected()) {
        return EIO;
    }

    if (read_only_) {
        return EROFS;  // Cannot create snapshots in read-only mode
    }

    // Snapshots require 3/3 quorum (not 2/3)
    if (connected_count_ < 3) {
        kprintf("[Crucible] Snapshot requires 3/3 downstairs (only %d connected)\n",
                connected_count_.load());
        return EIO;
    }

    try {
        // A snapshot Flush participates in the same ordered job chain.
        uint64_t job_id;
        if (flush_number_ == std::numeric_limits<uint64_t>::max()) {
            fence_session();
            return EIO;
        }
        auto dependencies = begin_job(job_id);
        ++flush_number_;

        // Create pending request (requires 3/3 acknowledgments)
        auto req = request_mgr_.create_request(job_id, MessageType::FlushAck, 0, 3);

        /*
         * Build a snapshot Flush.  The snapshot ID is stringified into
         * SnapshotDetails.snapshot_name, which is what upstream Crucible
         * accepts.  No per-extent limit.
         */
        char snapname[32];
        snprintf(snapname, sizeof(snapname), "%lu", (unsigned long)snapshot_id);

        Flush flush_msg;
        flush_msg.upstairs_id = upstairs_id_;
        flush_msg.session_id = session_id_;
        flush_msg.job_id = job_id;
        flush_msg.dependencies = std::move(dependencies);
        flush_msg.flush_number = flush_number_;
        flush_msg.gen_number = generation_;
        flush_msg.snapshot_name = std::string(snapname);
        flush_msg.extent_limit = nullopt;

        auto frame = encode_message(flush_msg);

        /*
         * Snapshots require 3/3 acknowledgement (not 2/3): a downstairs
         * that doesn't see the snapshot Flush would silently miss the
         * point-in-time the user asked us to capture.  Enqueue to all three.
         */
        int sent_count = 0;
        for (int i = 0; i < 3; i++) {
            if (enqueue_frame(i, frame)) {
                sent_count++;
            } else {
                req->mark_response(i, false, CrucibleError::ConnectionError);
            }
        }

        if (sent_count < 3) {
            request_mgr_.remove_request(job_id);
            for (int i = 0; i < 3; ++i) { fail_downstairs(i); }
            kprintf("[Crucible] snapshot %lu: only %d of 3 downstairs reachable\n",
                    snapshot_id, sent_count);
            return EIO;
        }

        if (!req->wait_for_quorum()) {
            request_mgr_.remove_request(job_id);
            for (int i = 0; i < 3; ++i) { fail_downstairs(i); }
            kprintf("[Crucible] snapshot %lu: 3/3 quorum not reached\n", snapshot_id);
            return EIO;
        }

        request_mgr_.remove_request(job_id);
        return 0;
    } catch (...) {
        fence_session();
        request_mgr_.cancel_all();
        throw;
    }
}

int UpsairsClient::discard_sync(uint64_t offset, uint64_t length)
{
    /*
     * The upstream Crucible protocol V13 has no Discard / DiscardAck
     * messages.  TRIM/UNMAP is silently unsupported here; report
     * ENOTSUP so the bio layer can complete the I/O and let the
     * filesystem fall back to overwrite-with-zero or skip the trim.
     */
    (void) offset;
    (void) length;
    return ENOTSUP;
}

std::pair<std::string, uint16_t> UpsairsClient::parse_target(const std::string& target)
{
    return parse_target_string(target);
}

void UpsairsClient::fence_session()
{
    // No allocation on this path: it also handles allocation failures after
    // partial enqueue. Never roll back a possibly transmitted job identifier.
    for (int i = 0; i < 3; ++i) {
        fail_downstairs(i);
    }
}

std::vector<uint64_t> UpsairsClient::begin_job(uint64_t& job_id)
{
    job_id = job_allocator_.allocate();
    if (!job_id || job_id == std::numeric_limits<uint64_t>::max()) {
        throw ConnectionError("Job identifier exhausted");
    }
    std::vector<uint64_t> deps;
    if (last_job_id_) {
        deps.push_back(last_job_id_);
    }
    last_job_id_ = job_id;
    return deps;
}

bool UpsairsClient::receive_available(int index, std::vector<uint8_t>& frame)
{
    auto& state = receive_[index];
    auto now = std::chrono::steady_clock::now();
    if (!state.bytes.empty() && now - state.started > std::chrono::seconds(5)) {
        throw ConnectionError("Partial frame deadline exceeded");
    }
    // One bounded chunk per peer per loop, so even a large frame cannot monopolize it.
    uint8_t chunk[16384];
    size_t size = std::min(sizeof(chunk), state.wanted - state.bytes.size());
    auto n = connections_[index]->recv_available(chunk, size);
    if (n < 0) {
        return false;
    }
    if (!n) {
        throw ConnectionError("Peer closed");
    }
    if (state.bytes.empty()) {
        state.started = now;
    }
    state.bytes.insert(state.bytes.end(), chunk, chunk + n);
    if (state.bytes.size() != state.wanted) {
        return false;
    }
    if (state.wanted == 4) {
        bincode::Decoder prefix(state.bytes);
        auto length = prefix.decode_u32();
        if (length < 8 || length > 2 * 1024 * 1024) {
            throw ConnectionError("Frame size out of range");
        }
        state.wanted = length;
        return false;
    }
    frame = std::move(state.bytes);
    frame.erase(frame.begin(), frame.begin() + 4);
    state.bytes = {};
    state.wanted = 4;
    return true;
}

void UpsairsClient::io_loop()
{
    auto next_ping = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (running_) {
        if (std::chrono::steady_clock::now() >= next_ping) {
            try {
                send_keepalive();
            } catch (...) {
                fence_session();
                return;
            }
            next_ping = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        }
        fd_set readfds;
        FD_ZERO(&readfds);
        int max_fd = -1;
        for (int i = 0; i < 3; ++i) {
            if (admitted_[i]) {
                int fd = connections_[i]->fd();
                if (fd >= FD_SETSIZE) {
                    fail_downstairs(i);
                    continue;
                }
                FD_SET(fd, &readfds);
                max_fd = std::max(max_fd, fd);
            }
        }
        timeval tv{0, 100000};
        int ret = select(max_fd + 1, &readfds, nullptr, nullptr, &tv);
        if (ret < 0 && errno == EINTR) {
            continue;
        }
        for (int i = 0; i < 3; ++i) {
            if (!admitted_[i]) {
                continue;
            }
            try {
                if (ret < 0) {
                    throw ConnectionError("select failed");
                }
                // Nonblocking receive also checks an unfinished frame's deadline.
                std::vector<uint8_t> frame;
                if (receive_available(i, frame)) {
                    process_responses(i, frame);
                }
            } catch (const std::exception& e) {
                kprintf("[Crucible] downstairs %d: %s\n", i, e.what());
                fail_downstairs(i);
            }
        }
    }
}

void UpsairsClient::send_keepalive()
{
    /*
     * Encode a single Ruok frame and enqueue it to every connected
     * downstairs sender.  Enqueue is the right primitive here: if a
     * downstairs's queue already has frames the link is actively sending
     * (not idle), so the ping harmlessly trails real traffic; if the link
     * is idle the queue is empty and the ping goes out immediately, well
     * inside the downstairs 45 s inactivity timeout.  io_loop never blocks
     * on the send path -- that work belongs to the sender threads now.
     */
    Ruok ping;
    auto frame = encode_message(ping);

    for (int i = 0; i < 3; i++) {
        enqueue_frame(i, frame);
    }
}

void UpsairsClient::process_responses(int index, const std::vector<uint8_t>& frame)
{
    bincode::Decoder dec(frame);
    auto type = decode_message_type(dec);
    if (type == MessageType::Imok && dec.at_end()) {
        return;
    }
    if (type != MessageType::WriteAck && type != MessageType::FlushAck &&
        type != MessageType::ReadResponse) {
        // Includes activation loss and ErrorReport: no repair/replay supported.
        throw ConnectionError("Unexpected data-plane message");
    }
    auto upstairs = dec.decode_uuid();
    auto session = dec.decode_uuid();
    auto job = dec.decode_u64();
    if (upstairs != upstairs_id_ || session != session_id_) {
        throw ConnectionError("Response session mismatch");
    }
    auto req = request_mgr_.find_request(job);
    if (req && req->expected_type != type) {
        throw ConnectionError("Response kind mismatch");
    }
    auto result = dec.decode_u32();
    if (result != 0) {
        // Conservatively quarantine any failed operation, including unsupported
        // variable-length error variants. Never credit it or retain broken deps.
        throw ConnectionError("Downstairs operation failed");
    }
    std::vector<uint8_t> data;
    if (type == MessageType::ReadResponse) {
        auto count = dec.decode_u64();
        if (count > (1024 * 1024) / block_size_ || count > dec.remaining() / 4 ||
            (req && count != req->read_length / block_size_)) {
            throw ConnectionError("Read context count mismatch");
        }
        std::vector<ReadBlockContext> contexts;
        contexts.reserve(count);
        for (uint64_t i = 0; i < count; ++i) {
            ReadBlockContext ctx{};
            ctx.type = static_cast<ReadBlockType>(dec.decode_u32());
            if (ctx.type == ReadBlockType::Unencrypted) {
                ctx.hash = dec.decode_u64();
            } else if (ctx.type != ReadBlockType::Empty) {
                throw ConnectionError("Unsupported read block type");
            }
            contexts.push_back(ctx);
        }
        auto length = dec.decode_u64();
        if (count > dec.remaining() / block_size_ || length != count * block_size_ ||
            length != dec.remaining() || (req && length != req->read_length)) {
            throw ConnectionError("Read length mismatch");
        }
        data = dec.decode_bytes(length);
        for (size_t i = 0; i < contexts.size(); ++i) {
            auto block = data.data() + i * block_size_;
            const auto& ctx = contexts[i];
            if (ctx.type == ReadBlockType::Empty) {
                if (std::any_of(block, block + block_size_, [](uint8_t c) { return c != 0; })) {
                    throw ConnectionError("Nonzero data without integrity context");
                }
            } else if (xxhash64_block(block, block_size_) != ctx.hash) {
                throw ConnectionError("Read integrity hash mismatch");
            }
        }
    }
    if (!dec.at_end()) {
        throw ConnectionError("Trailing response data");
    }
    // Even a late third response is validated before being discarded.
    if (req) {
        WITH_LOCK(req->mtx) {
            if (req->completed || req->downstairs_responded[index]) {
                return;
            }
            req->read_data[index] = std::move(data);
        }
        req->mark_response(index, true);
    }
}

void UpsairsClient::handshake(int downstairs_idx, std::chrono::steady_clock::time_point deadline)
{
    auto& conn = connections_[downstairs_idx];
    if (!conn || !conn->is_connected()) {
        throw ConnectionError("Downstairs not connected");
    }

    // Send HereIAm
    HereIAm here_msg;
    here_msg.version = static_cast<uint32_t>(ProtocolVersion::V13);
    here_msg.upstairs_id = upstairs_id_;
    here_msg.session_id = session_id_;
    here_msg.gen = generation_;
    here_msg.read_only = read_only_;
    here_msg.encrypted = encrypted_;
    /* No alternate versions advertised - upstairs only speaks V13. */
    here_msg.alternate_versions.clear();

    auto frame = encode_message(here_msg);
    conn->send_exact(frame.data(), frame.size(), deadline);

    // Receive response
    auto response_frame = receive_frame(downstairs_idx);
    bincode::Decoder dec(response_frame);

    MessageType type = decode_message_type(dec);

    if (type == MessageType::YesItsMe) {
        auto yes_msg = YesItsMe::decode(dec);
        if (!dec.at_end()) { throw ConnectionError("Trailing handshake data"); }

        if (yes_msg.version != static_cast<uint32_t>(ProtocolVersion::V13)) {
            throw std::runtime_error("Version mismatch in YesItsMe: got " +
                                      std::to_string(yes_msg.version));
        }

        /*
         * Upstream Crucible protocol step 2: send PromoteToActive and wait
         * for YouAreNowActive (or YouAreNoLongerActive if another upstairs
         * has stolen the slot).  Only after this exchange may we issue
         * RegionInfoPlease and start I/O.
         */
        PromoteToActive promote;
        promote.upstairs_id = upstairs_id_;
        promote.session_id = session_id_;
        promote.generation = generation_;
        auto promote_frame = encode_message(promote);
        conn->send_exact(promote_frame.data(), promote_frame.size(), deadline);

        auto resp_frame = receive_frame(downstairs_idx);
        bincode::Decoder resp_dec(resp_frame);
        MessageType resp_type = decode_message_type(resp_dec);
        if (resp_type == MessageType::YouAreNowActive) {
            auto active = YouAreNowActive::decode(resp_dec);
            if (!resp_dec.at_end()) { throw ConnectionError("Trailing activation data"); }
            if (active.upstairs_id != upstairs_id_ || active.session_id != session_id_ ||
                active.generation != generation_) {
                throw ConnectionError("Activation identity mismatch");
            }
            kprintf("[Crucible] downstairs %d active (repair port %u)\n",
                    downstairs_idx, yes_msg.repair_addr.port);
        } else if (resp_type == MessageType::YouAreNoLongerActive) {
            throw std::runtime_error("Promote rejected: another session is active");
        } else {
            throw std::runtime_error("Unexpected promote response: " +
                                     std::to_string(static_cast<uint32_t>(resp_type)));
        }

    } else if (type == MessageType::VersionMismatch) {
        auto mismatch = VersionMismatch::decode(dec);
        throw std::runtime_error("Version mismatch: offered " +
                                std::to_string(mismatch.offered));

    } else if (type == MessageType::ReadOnlyMismatch) {
        throw std::runtime_error("Read-only mode mismatch");

    } else if (type == MessageType::EncryptedMismatch) {
        throw std::runtime_error("Encryption mode mismatch");

    } else if (type == MessageType::UuidMismatch) {
        throw std::runtime_error("Region UUID mismatch");

    } else {
        throw std::runtime_error("Unexpected handshake response: " +
                                std::to_string(static_cast<uint32_t>(type)));
    }
}

void UpsairsClient::query_region_info(int downstairs_idx, std::chrono::steady_clock::time_point deadline)
{
    auto& conn = connections_[downstairs_idx];
    if (!conn || !conn->is_connected()) {
        throw ConnectionError("Downstairs not connected");
    }

    RegionInfoPlease req_msg;
    auto frame = encode_message(req_msg);
    conn->send_exact(frame.data(), frame.size(), deadline);

    auto response_frame = receive_frame(downstairs_idx);
    bincode::Decoder dec(response_frame);

    MessageType type = decode_message_type(dec);
    if (type != MessageType::RegionInfo) {
        throw std::runtime_error("Expected RegionInfo, got " +
                                std::to_string(static_cast<uint32_t>(type)));
    }

    auto info_msg = RegionInfo::decode(dec);
    if (!dec.at_end()) { throw ConnectionError("Trailing region data"); }
    const auto& def = info_msg.region_def;
    if (downstairs_idx == 0) {
        region_def_ = def;
    } else if (def.block_size != region_def_.block_size ||
               def.extent_size != region_def_.extent_size ||
               def.extent_size_shift != region_def_.extent_size_shift ||
               def.extent_count != region_def_.extent_count ||
               def.encrypted != region_def_.encrypted) {
        throw ConnectionError("Replica geometry mismatch");
    }
    if (def.encrypted || !def.extent_size || !def.extent_count ||
        def.extent_size > std::numeric_limits<uint64_t>::max() / def.extent_count) {
        throw ConnectionError("Invalid region geometry");
    }

    // Validate region definition
    if (region_def_.block_size != block_size_) {
        throw std::runtime_error("Block size mismatch: expected " +
                                std::to_string(block_size_) + ", got " +
                                std::to_string(region_def_.block_size));
    }

    /*
     * Set total_blocks_ from the region definition the first time we
     * see it; subsequent downstairs are validated against the same
     * geometry by the block_size check above.
     */
    if (total_blocks_ == 0) {
        total_blocks_ = region_def_.extent_size * region_def_.extent_count;
        kprintf("[Crucible] region: %lu blocks of %lu bytes (%u extents x %lu)\n",
                total_blocks_, region_def_.block_size,
                region_def_.extent_count, region_def_.extent_size);
    }

    // Keep metadata for all-three clean-cohort admission, never hash-based freshness.
    ExtentVersionsPlease ev_req;
    auto ev_frame = encode_message(ev_req);
    conn->send_exact(ev_frame.data(), ev_frame.size(), deadline);

    auto ev_resp_frame = receive_frame(downstairs_idx);
    bincode::Decoder ev_dec(ev_resp_frame);
    MessageType ev_type = decode_message_type(ev_dec);
    if (ev_type != MessageType::ExtentVersions) {
        throw std::runtime_error("Expected ExtentVersions, got " +
                                  std::to_string(static_cast<uint32_t>(ev_type)));
    }
    versions_[downstairs_idx] = ExtentVersions::decode(ev_dec);
    if (!ev_dec.at_end()) { throw ConnectionError("Trailing extent metadata"); }
}

void UpsairsClient::validate_cohort()
{
    uint64_t max_flush = 0;
    for (const auto& v : versions_) {
        if (v.gen_numbers.size() != region_def_.extent_count ||
            v.flush_numbers.size() != region_def_.extent_count ||
            v.dirty_bits.size() != region_def_.extent_count ||
            v.gen_numbers != versions_[0].gen_numbers ||
            v.flush_numbers != versions_[0].flush_numbers) {
            throw ConnectionError("Replica metadata needs external reconciliation");
        }
        for (size_t i = 0; i < v.gen_numbers.size(); ++i) {
            if (v.dirty_bits[i] || v.gen_numbers[i] >= generation_) {
                throw ConnectionError("Dirty extent or external generation too low");
            }
            max_flush = std::max(max_flush, v.flush_numbers[i]);
        }
    }
    if (max_flush == std::numeric_limits<uint64_t>::max()) {
        throw ConnectionError("Flush number exhausted");
    }
    // This is only a flush sequence, NOT permission to invent a fencing epoch.
    flush_number_ = max_flush;
}

std::vector<uint8_t> UpsairsClient::receive_frame(int downstairs_idx)
{
    auto& conn = connections_[downstairs_idx];
    if (!conn || !conn->is_connected()) {
        throw ConnectionError("Downstairs not connected");
    }

    /*
     * The Rust upstream Crucible encoder writes the 4-byte u32 LE prefix
     * as TOTAL frame length (prefix + payload), so subtract 4 to get the
     * payload length we still need to read.
     */
    uint32_t total_length;
    conn->recv_exact(&total_length, 4);

    if (total_length < 8 || total_length > 2 * 1024 * 1024) {
        throw std::runtime_error("Frame size out of range");
    }
    uint32_t payload_length = total_length - 4;

    std::vector<uint8_t> data(payload_length);
    conn->recv_exact(data.data(), payload_length);

    return data;
}

} // namespace crucible
