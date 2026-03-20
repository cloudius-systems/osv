/*
 * Copyright (C) 2024 Waldemar Kozaczuk
 * Copyright (C) 2024 OSv Contributors
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef CRUCIBLE_MESSAGES_HH
#define CRUCIBLE_MESSAGES_HH

#include "crucible-types.hh"
#include "crucible-bincode.hh"
#include <vector>
#include <optional>

namespace crucible {

/**
 * Message variant discriminants (u32 LE on the wire).
 *
 * Order MUST match the upstream Rust `Message` enum in
 * external Crucible's protocol/src/lib.rs exactly: bincode encodes the
 * discriminant as variant_index, so reordering or inserting variants
 * shifts every subsequent variant's number on the wire.
 *
 * Variants the OSv driver does not currently use (PromoteToActive,
 * YouAreNowActive, ExtentLive*, etc.) are listed for index alignment
 * even though we do not encode/decode them.
 */
enum class MessageType : uint32_t {
    HereIAm                 = 0,
    YesItsMe                = 1,
    VersionMismatch         = 2,
    ReadOnlyMismatch        = 3,
    EncryptedMismatch       = 4,
    PromoteToActive         = 5,
    YouAreNowActive         = 6,
    YouAreNoLongerActive    = 7,
    UuidMismatch            = 8,
    Ruok                    = 9,
    Imok                    = 10,
    ExtentClose             = 11,
    ExtentReopen            = 12,
    ExtentFlush             = 13,
    ExtentRepair            = 14,
    RepairAckId             = 15,
    ExtentError             = 16,
    ExtentLiveClose         = 17,
    ExtentLiveFlushClose    = 18,
    ExtentLiveRepair        = 19,
    ExtentLiveReopen        = 20,
    ExtentLiveNoOp          = 21,
    ExtentLiveCloseAck      = 22,
    ExtentLiveRepairAckId   = 23,
    ExtentLiveAckId         = 24,
    RegionInfoPlease        = 25,
    RegionInfo              = 26,
    ExtentVersionsPlease    = 27,
    ExtentVersions          = 28,
    LastFlush               = 29,
    Write                   = 30,
    WriteAck                = 31,
    Flush                   = 32,
    FlushAck                = 33,
    Barrier                 = 34,
    BarrierAck              = 35,
    ReadRequest             = 36,
    ReadResponse            = 37,
    WriteUnwritten          = 38,
    WriteUnwrittenAck       = 39,
    ErrorReport             = 40,
};

/**
 * Socket address (simplified - IPv4 only for now).
 */
struct SocketAddr {
    uint32_t ip;     // IPv4 address (network byte order)
    uint16_t port;   // Port number

    SocketAddr() : ip(0), port(0) {}
    SocketAddr(uint32_t ip, uint16_t port) : ip(ip), port(port) {}
};

// ============================================================================
// Negotiation Messages
// ============================================================================

/**
 * HereIAm - Initial handshake from upstairs to downstairs.
 */
struct HereIAm {
    static constexpr MessageType TYPE = MessageType::HereIAm;

    uint32_t version;                          // Protocol version (13)
    Uuid upstairs_id;                          // Persistent upstairs UUID
    Uuid session_id;                           // Session UUID
    uint64_t gen;                              // Generation number
    bool read_only;                            // Read-only mode
    bool encrypted;                            // Encryption expected
    std::vector<uint32_t> alternate_versions;  // Other accepted versions

    void encode(bincode::Encoder& enc) const {
        enc.encode_u32(static_cast<uint32_t>(TYPE));
        enc.encode_u32(version);
        enc.encode_uuid(upstairs_id);
        enc.encode_uuid(session_id);
        enc.encode_u64(gen);
        enc.encode_bool(read_only);
        enc.encode_bool(encrypted);
        enc.encode_vec<uint32_t>(alternate_versions,
                                  [&](uint32_t v) { enc.encode_u32(v); });
    }
};

/**
 * YesItsMe - Response from downstairs accepting connection.
 *
 * Wire format matches the upstream Rust enum variant exactly:
 *     YesItsMe { version: u32, repair_addr: SocketAddr }
 *
 * SocketAddr is a Rust std::net::SocketAddr with serde's default
 * representation: a u32 LE variant tag (0 for V4, 1 for V6) followed by
 * the address bytes (4 for V4, 16 for V6) and a u16 LE port.
 *
 * IMPORTANT: this struct previously contained upstairs_id, session_id,
 * gen, and an optional<SocketAddr> — none of which exist in the upstream
 * protocol's YesItsMe.  Decoding the response with the old layout
 * consumed bytes belonging to subsequent messages, leading to
 * out-of-band errors during the next read.
 */
struct YesItsMe {
    static constexpr MessageType TYPE = MessageType::YesItsMe;

    uint32_t version;
    SocketAddr repair_addr;

    static YesItsMe decode(bincode::Decoder& dec) {
        YesItsMe msg;
        // Discriminant already consumed by decode_message_type()
        msg.version = dec.decode_u32();
        uint32_t af_tag = dec.decode_u32();   // 0 = V4, 1 = V6
        if (af_tag == 0) {
            uint32_t ip = dec.decode_u32();   // 4 bytes, network order
            uint16_t port = dec.decode_u16();
            msg.repair_addr = SocketAddr(ip, port);
        } else {
            // IPv6: 16 bytes address + u16 port; not used by OSv, just skip
            dec.skip(16 + 2);
        }
        return msg;
    }
};

/**
 * VersionMismatch - Protocol version incompatible.
 */
struct VersionMismatch {
    static constexpr MessageType TYPE = MessageType::VersionMismatch;
    uint32_t offered;  // Version offered by upstairs

    static VersionMismatch decode(bincode::Decoder& dec) {
        return {dec.decode_u32()};
    }
};

/**
 * PromoteToActive - upstairs promotes itself to the active session.
 *
 * Sent by the upstairs after receiving YesItsMe.  The downstairs
 * replies with YouAreNowActive (or YouAreNoLongerActive if a newer
 * session has taken over).
 */
struct PromoteToActive {
    static constexpr MessageType TYPE = MessageType::PromoteToActive;

    Uuid upstairs_id;
    Uuid session_id;
    uint64_t generation;

    void encode(bincode::Encoder& enc) const {
        enc.encode_u32(static_cast<uint32_t>(TYPE));
        enc.encode_uuid(upstairs_id);
        enc.encode_uuid(session_id);
        enc.encode_u64(generation);
    }
};

/**
 * YouAreNowActive - downstairs accepts the promotion.
 */
struct YouAreNowActive {
    static constexpr MessageType TYPE = MessageType::YouAreNowActive;

    Uuid upstairs_id;
    Uuid session_id;
    uint64_t generation;

    static YouAreNowActive decode(bincode::Decoder& dec) {
        YouAreNowActive msg;
        msg.upstairs_id = dec.decode_uuid();
        msg.session_id = dec.decode_uuid();
        msg.generation = dec.decode_u64();
        return msg;
    }
};

// ============================================================================
// Metadata Messages
// ============================================================================

/**
 * RegionInfoPlease - Request region information.
 */
struct RegionInfoPlease {
    static constexpr MessageType TYPE = MessageType::RegionInfoPlease;

    void encode(bincode::Encoder& enc) const {
        enc.encode_u32(static_cast<uint32_t>(TYPE));
    }
};

/**
 * RegionInfo - Region definition response.
 */
struct RegionInfo {
    static constexpr MessageType TYPE = MessageType::RegionInfo;

    RegionDefinition region_def;

    /*
     * Layout matches upstream Crucible's RegionDefinition serde encoding:
     *   block_size: u64
     *   extent_size: Block { value: u64, shift: u32 }
     *   extent_count: u32
     *   uuid: Uuid (bincode bytes: u64 length=16 + 16 bytes)
     *   encrypted: bool
     *   database_read_version: usize (u64 on 64-bit)
     *   database_write_version: usize
     */
    static RegionInfo decode(bincode::Decoder& dec) {
        RegionInfo msg;
        msg.region_def.block_size       = dec.decode_u64();
        msg.region_def.extent_size      = dec.decode_u64();
        msg.region_def.extent_size_shift = dec.decode_u32();
        msg.region_def.extent_count     = dec.decode_u32();
        msg.region_def.uuid             = dec.decode_uuid();
        msg.region_def.encrypted        = dec.decode_bool();
        msg.region_def.database_read_version  = dec.decode_u64();
        msg.region_def.database_write_version = dec.decode_u64();
        return msg;
    }
};

/**
 * ExtentVersionsPlease - request the per-extent flush state from a
 * downstairs as part of the negotiation handshake.  Sent after RegionInfo.
 */
struct ExtentVersionsPlease {
    static constexpr MessageType TYPE = MessageType::ExtentVersionsPlease;

    void encode(bincode::Encoder& enc) const {
        enc.encode_u32(static_cast<uint32_t>(TYPE));
    }
};

/**
 * ExtentVersions - per-extent flush state response.
 *
 * Three vectors of length extent_count:
 *   gen_numbers:   Vec<u64>
 *   flush_numbers: Vec<u64>
 *   dirty_bits:    Vec<bool>
 *
 * The OSv driver does not consume these for now (no live-repair logic);
 * we just need to drain the bytes so the downstairs can transition to
 * WaitQuorum and start servicing I/O.
 */
struct ExtentVersions {
    static constexpr MessageType TYPE = MessageType::ExtentVersions;

    std::vector<uint64_t> gen_numbers;
    std::vector<uint64_t> flush_numbers;
    std::vector<bool>     dirty_bits;

    static ExtentVersions decode(bincode::Decoder& dec) {
        ExtentVersions msg;
        msg.gen_numbers   = dec.decode_vec<uint64_t>(
            [&]() { return dec.decode_u64(); });
        msg.flush_numbers = dec.decode_vec<uint64_t>(
            [&]() { return dec.decode_u64(); });
        msg.dirty_bits    = dec.decode_vec<bool>(
            [&]() { return dec.decode_bool(); });
        return msg;
    }
};

// ============================================================================
// IO Operations
// ============================================================================

/**
 * Write - Write operation to downstairs.
 *
 * Upstream wire format:
 *   Write { header: WriteHeader, data: bytes::Bytes }
 *
 * where WriteHeader is:
 *   { upstairs_id: Uuid, session_id: Uuid, job_id: JobId(u64),
 *     dependencies: Vec<JobId>, start: BlockIndex(u64),
 *     contexts: Vec<BlockContext> }
 *
 * The data field appears inline in the same bincode-serialized frame, with
 * a u64 LE length prefix (bincode's standard `bytes` representation).  The
 * data length must equal `contexts.len() * block_size`.
 *
 * Use encode_message_with_data_header() to produce the wire frame.  The
 * returned bytes end at the u64 data length; the caller must send the
 * actual block data immediately afterwards on the same socket.
 */
struct Write {
    static constexpr MessageType TYPE = MessageType::Write;

    Uuid upstairs_id;
    Uuid session_id;
    uint64_t job_id;
    std::vector<uint64_t> dependencies;
    uint64_t start_block;
    std::vector<BlockContext> contexts;

    void encode_header(bincode::Encoder& enc, uint64_t data_len) const {
        enc.encode_u32(static_cast<uint32_t>(TYPE));
        enc.encode_uuid(upstairs_id);
        enc.encode_uuid(session_id);
        enc.encode_u64(job_id);
        enc.encode_vec<uint64_t>(dependencies, [&](uint64_t dep) {
            enc.encode_u64(dep);
        });
        enc.encode_u64(start_block);
        enc.encode_vec<BlockContext>(contexts, [&](const BlockContext& ctx) {
            enc.encode_u64(ctx.hash);
            enc.encode_option<EncryptionContext>(ctx.encryption_ctx,
                [&](const EncryptionContext& ectx) {
                    enc.encode_encryption_context(ectx);
                });
        });
        /* bincode `bytes::Bytes` length prefix for the inline data field. */
        enc.encode_u64(data_len);
    }
};

/**
 * WriteAck - Acknowledgment of write operation.
 */
struct WriteAck {
    static constexpr MessageType TYPE = MessageType::WriteAck;

    Uuid upstairs_id;
    Uuid session_id;
    uint64_t job_id;
    Result<void> result;

    static WriteAck decode(bincode::Decoder& dec) {
        WriteAck msg;
        msg.upstairs_id = dec.decode_uuid();
        msg.session_id = dec.decode_uuid();
        msg.job_id = dec.decode_u64();

        /*
         * Result<T, E> in bincode 1.x is a regular enum: u32 LE variant
         * tag (0 = Ok, 1 = Err) followed by the inner value.  The OSv
         * decoder previously read this as a u8 which left 3 bytes of
         * Result-tag bleeding into subsequent fields.
         */
        uint32_t result_tag = dec.decode_u32();
        if (result_tag == 0) {
            msg.result = Result<void>::ok();
        } else {
            uint32_t error = dec.decode_u32();
            msg.result = Result<void>::err(static_cast<CrucibleError>(error));
        }
        return msg;
    }
};

/**
 * ReadRequest - Read operation request.
 */
struct ReadRequest {
    static constexpr MessageType TYPE = MessageType::ReadRequest;

    Uuid upstairs_id;
    Uuid session_id;
    uint64_t job_id;
    std::vector<uint64_t> dependencies;
    uint64_t start_block;
    uint64_t count;  // Number of blocks

    void encode(bincode::Encoder& enc) const {
        enc.encode_u32(static_cast<uint32_t>(TYPE));
        enc.encode_uuid(upstairs_id);
        enc.encode_uuid(session_id);
        enc.encode_u64(job_id);

        enc.encode_vec<uint64_t>(dependencies, [&](uint64_t dep) {
            enc.encode_u64(dep);
        });

        enc.encode_u64(start_block);
        enc.encode_u64(count);
    }
};

/**
 * ReadResponse - Read operation response.
 *
 * Upstream wire format:
 *   ReadResponse { header: ReadResponseHeader, data: bytes::BytesMut }
 *
 * where ReadResponseHeader is:
 *   { upstairs_id: Uuid, session_id: Uuid, job_id: JobId,
 *     blocks: Result<Vec<ReadBlockContext>, CrucibleError> }
 *
 * The data bytes are encoded inline in the same frame with a u64 length
 * prefix.  After decoding the header, the caller must consume the u64
 * data length and that many bytes from the same frame buffer (use
 * decode_byte_slice_length() and copy).
 */
struct ReadResponse {
    static constexpr MessageType TYPE = MessageType::ReadResponse;

    Uuid upstairs_id;
    Uuid session_id;
    uint64_t job_id;
    Result<std::vector<ReadBlockContext>> blocks;

    /*
     * Decode the header portion only.  The decoder is left positioned at
     * the u64 data length prefix so the caller can drain the inline data.
     */
    static ReadResponse decode_header(bincode::Decoder& dec) {
        ReadResponse msg;
        msg.upstairs_id = dec.decode_uuid();
        msg.session_id = dec.decode_uuid();
        msg.job_id = dec.decode_u64();

        /* Result<T, E> uses a u32 LE variant tag in bincode. */
        uint32_t result_tag = dec.decode_u32();
        if (result_tag == 0) {
            uint64_t len = dec.decode_u64();
            std::vector<ReadBlockContext> contexts;
            contexts.reserve(len);
            for (uint64_t i = 0; i < len; i++) {
                ReadBlockContext ctx;
                uint32_t type_disc = dec.decode_u32();
                ctx.type = static_cast<ReadBlockType>(type_disc);
                if (ctx.type == ReadBlockType::Empty) {
                    /* no payload */
                } else if (ctx.type == ReadBlockType::Encrypted) {
                    ctx.encryption_ctx = dec.decode_encryption_context();
                } else if (ctx.type == ReadBlockType::Unencrypted) {
                    ctx.hash = dec.decode_u64();
                }
                contexts.push_back(ctx);
            }
            msg.blocks = Result<std::vector<ReadBlockContext>>::ok(std::move(contexts));
        } else {
            uint32_t error = dec.decode_u32();
            msg.blocks = Result<std::vector<ReadBlockContext>>::err(
                static_cast<CrucibleError>(error));
        }
        return msg;
    }

    /* Backward-compatible name; same as decode_header. */
    static ReadResponse decode(bincode::Decoder& dec) {
        return decode_header(dec);
    }
};

/**
 * Flush - Flush operation.
 */
/*
 * Upstream wire format:
 *   Flush {
 *     upstairs_id: Uuid,
 *     session_id: Uuid,
 *     job_id: JobId(u64),
 *     dependencies: Vec<JobId>,
 *     flush_number: u64,
 *     gen_number: u64,
 *     snapshot_details: Option<SnapshotDetails { snapshot_name: String }>,
 *     extent_limit: Option<ExtentId(u32)>,
 *   }
 *
 * The OSv driver does not take snapshots and does not pipeline flushes
 * across extent ranges, so both optionals are encoded as None (a single
 * 0 byte) by default.  snapshot_name -- when set -- goes on the wire as
 * a bincode String (u64 length + UTF-8 bytes).
 */
struct Flush {
    static constexpr MessageType TYPE = MessageType::Flush;

    Uuid upstairs_id;
    Uuid session_id;
    uint64_t job_id;
    std::vector<uint64_t> dependencies;
    uint64_t flush_number;
    uint64_t gen_number;
    optional<std::string>  snapshot_name;   // Some => SnapshotDetails { name }
    optional<uint32_t>     extent_limit;    // Option<ExtentId(u32)>

    void encode(bincode::Encoder& enc) const {
        enc.encode_u32(static_cast<uint32_t>(TYPE));
        enc.encode_uuid(upstairs_id);
        enc.encode_uuid(session_id);
        enc.encode_u64(job_id);
        enc.encode_vec<uint64_t>(dependencies, [&](uint64_t dep) {
            enc.encode_u64(dep);
        });
        enc.encode_u64(flush_number);
        enc.encode_u64(gen_number);
        enc.encode_option<std::string>(snapshot_name, [&](const std::string& s) {
            /* SnapshotDetails is a single-field tuple struct; bincode
             * flattens the inner String { u64 length + bytes }. */
            enc.encode_string(s);
        });
        enc.encode_option<uint32_t>(extent_limit, [&](uint32_t e) {
            enc.encode_u32(e);
        });
    }
};

/**
 * FlushAck - Flush acknowledgment.
 */
struct FlushAck {
    static constexpr MessageType TYPE = MessageType::FlushAck;

    Uuid upstairs_id;
    Uuid session_id;
    uint64_t job_id;
    Result<void> result;

    static FlushAck decode(bincode::Decoder& dec) {
        FlushAck msg;
        msg.upstairs_id = dec.decode_uuid();
        msg.session_id = dec.decode_uuid();
        msg.job_id = dec.decode_u64();

        /* Result<T, E> uses a u32 LE variant tag in bincode. */
        uint32_t result_tag = dec.decode_u32();
        if (result_tag == 0) {
            msg.result = Result<void>::ok();
        } else {
            uint32_t error = dec.decode_u32();
            msg.result = Result<void>::err(static_cast<CrucibleError>(error));
        }
        return msg;
    }
};

/*
 * Discard / DiscardAck did not survive into the upstream Crucible
 * protocol V13.  The OSv driver previously declared local variants for
 * them; they have been removed because the downstairs would receive an
 * unknown discriminant and disconnect (or worse, panic).  BIO_DISCARD
 * is mapped to ENOTSUP at the bio layer for now.
 */

// ============================================================================
// Control Messages
// ============================================================================

/**
 * Ruok - Health check request ("Are you OK?").
 */
struct Ruok {
    static constexpr MessageType TYPE = MessageType::Ruok;

    void encode(bincode::Encoder& enc) const {
        enc.encode_u32(static_cast<uint32_t>(TYPE));
    }
};

/**
 * Imok - Health check response ("I'm OK").
 */
struct Imok {
    static constexpr MessageType TYPE = MessageType::Imok;

    static Imok decode(bincode::Decoder& /*dec*/) {
        return Imok{};
    }
};

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * Decode message type discriminant.
 */
inline MessageType decode_message_type(bincode::Decoder& dec) {
    uint32_t disc = dec.decode_u32();
    return static_cast<MessageType>(disc);
}

/*
 * Frame format on the wire (matches upstream Crucible protocol/src/lib.rs):
 *
 *   [u32 LE total_length][bincode-serialized Message]
 *
 * total_length is the FULL frame size (the 4-byte prefix + payload).
 *
 * For messages that contain bulk data (Write, WriteUnwritten, ReadResponse),
 * the data field is encoded inline as bincode `bytes` -- a u64 LE length
 * followed by the raw bytes -- so a Write of 4 KiB across two blocks is a
 * single frame:
 *
 *   [u32 total][u32 variant_tag][header fields...][u64 data_len=8192][8192 bytes]
 */

/**
 * Encode a message with no trailing bulk data.
 */
template<typename T>
std::vector<uint8_t> encode_message(const T& msg) {
    bincode::Encoder enc;
    msg.encode(enc);
    auto payload = enc.take();

    std::vector<uint8_t> frame;
    frame.reserve(4 + payload.size());

    uint32_t length = static_cast<uint32_t>(4 + payload.size());
    frame.push_back(length & 0xFF);
    frame.push_back((length >> 8) & 0xFF);
    frame.push_back((length >> 16) & 0xFF);
    frame.push_back((length >> 24) & 0xFF);

    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

/**
 * Encode a message that carries trailing bulk data inline.
 *
 * The header part (variant tag + struct fields + u64 data length prefix)
 * is produced by msg.encode_header(enc, data_len).  The frame's u32 prefix
 * accounts for header + data_len + actual data bytes.  The caller is
 * expected to send the returned vector immediately followed by exactly
 * `data_len` bytes of `data` over the same socket; the result vector
 * holds `frame_prefix || header || u64 data_len`, ready to be followed
 * by `data` via send_exact.
 */
template<typename T>
std::vector<uint8_t> encode_message_with_data_header(const T& msg,
                                                     uint64_t data_len) {
    bincode::Encoder enc;
    msg.encode_header(enc, data_len);
    auto header = enc.take();

    /*
     * The header portion already includes the u64 length prefix that
     * bincode writes for `bytes::Bytes`.  The frame total is the 4-byte
     * prefix plus the bincode header bytes plus the actual data bytes.
     */
    uint32_t total = static_cast<uint32_t>(4 + header.size() + data_len);
    std::vector<uint8_t> frame;
    frame.reserve(4 + header.size());

    frame.push_back(total & 0xFF);
    frame.push_back((total >> 8) & 0xFF);
    frame.push_back((total >> 16) & 0xFF);
    frame.push_back((total >> 24) & 0xFF);
    frame.insert(frame.end(), header.begin(), header.end());
    return frame;
}

/**
 * Decode message from vector (length prefix already removed).
 */
template<typename T>
T decode_message(const std::vector<uint8_t>& data) {
    bincode::Decoder dec(data);
    MessageType type = decode_message_type(dec);

    if (type != T::TYPE) {
        throw std::runtime_error("Message type mismatch");
    }

    return T::decode(dec);
}

} // namespace crucible

#endif // CRUCIBLE_MESSAGES_HH
