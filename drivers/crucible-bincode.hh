/*
 * Copyright (C) 2024 Waldemar Kozaczuk
 * Copyright (C) 2024 OSv Contributors
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef CRUCIBLE_BINCODE_HH
#define CRUCIBLE_BINCODE_HH

#include "crucible-types.hh"
#include <vector>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace crucible {
namespace bincode {

/**
 * Bincode encoder for Crucible protocol.
 *
 * Implements Rust's bincode serialization format:
 * - Little-endian byte order
 * - Fixed-size integers encoded directly
 * - Vectors: length (u64) followed by elements
 * - Enums: discriminant (u32) followed by variant data
 * - Option: 0 for None, 1 for Some + value
 * - Result: 0 for Ok, 1 for Err + value/error
 */
class Encoder {
public:
    Encoder() = default;

    /**
     * Get encoded data.
     */
    const std::vector<uint8_t>& data() const { return buffer_; }

    /**
     * Take ownership of encoded data.
     */
    std::vector<uint8_t> take() { return std::move(buffer_); }

    /**
     * Clear the buffer.
     */
    void clear() { buffer_.clear(); }

    /**
     * Get current size.
     */
    size_t size() const { return buffer_.size(); }

    // Primitive types

    void encode_u8(uint8_t val) {
        buffer_.push_back(val);
    }

    void encode_u16(uint16_t val) {
        uint8_t bytes[2];
        bytes[0] = val & 0xFF;
        bytes[1] = (val >> 8) & 0xFF;
        buffer_.insert(buffer_.end(), bytes, bytes + 2);
    }

    void encode_u32(uint32_t val) {
        uint8_t bytes[4];
        bytes[0] = val & 0xFF;
        bytes[1] = (val >> 8) & 0xFF;
        bytes[2] = (val >> 16) & 0xFF;
        bytes[3] = (val >> 24) & 0xFF;
        buffer_.insert(buffer_.end(), bytes, bytes + 4);
    }

    void encode_u64(uint64_t val) {
        uint8_t bytes[8];
        for (int i = 0; i < 8; i++) {
            bytes[i] = (val >> (i * 8)) & 0xFF;
        }
        buffer_.insert(buffer_.end(), bytes, bytes + 8);
    }

    void encode_bool(bool val) {
        encode_u8(val ? 1 : 0);
    }

    // Crucible types

    /*
     * Encode a UUID as Rust serde does: serializer.serialize_bytes(&[u8;16]),
     * which under bincode's standard config writes a u64 little-endian length
     * prefix (=16) followed by the 16 raw bytes.
     */
    void encode_uuid(const Uuid& uuid) {
        encode_u64(16);
        buffer_.insert(buffer_.end(), uuid.bytes, uuid.bytes + 16);
    }

    void encode_encryption_context(const EncryptionContext& ctx) {
        buffer_.insert(buffer_.end(), ctx.nonce, ctx.nonce + 12);
        buffer_.insert(buffer_.end(), ctx.tag, ctx.tag + 16);
    }

    // Option<T>
    template<typename T, typename EncodeFunc>
    void encode_option(const optional<T>& opt, EncodeFunc encode_fn) {
        if (opt.has_value()) {
            encode_u8(1);  // Some
            encode_fn(*opt);
        } else {
            encode_u8(0);  // None
        }
    }

    // Vec<T>
    template<typename T, typename EncodeFunc>
    void encode_vec(const std::vector<T>& vec, EncodeFunc encode_fn) {
        encode_u64(vec.size());
        for (const auto& item : vec) {
            encode_fn(item);
        }
    }

    /*
     * Result<T, E> in bincode 1.x is a regular enum: u32 LE variant tag
     * (0 = Ok, 1 = Err) followed by the inner value.  Note this is
     * different from Option, which uses a u8 tag.
     */
    template<typename T, typename EncodeOkFunc, typename EncodeErrFunc>
    void encode_result(const Result<T>& result, EncodeOkFunc encode_ok,
                       EncodeErrFunc encode_err) {
        if (result.is_ok) {
            encode_u32(0);  // Ok
            encode_ok(result.value);
        } else {
            encode_u32(1);  // Err
            encode_err(result.error);
        }
    }

    // String (Vec<u8>)
    void encode_string(const std::string& str) {
        encode_u64(str.size());
        buffer_.insert(buffer_.end(), str.begin(), str.end());
    }

    // Raw bytes — append without prefix.  Used internally by encoders that
    // already wrote a length prefix themselves.
    void encode_bytes(const void* data, size_t len) {
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        buffer_.insert(buffer_.end(), bytes, bytes + len);
    }

    /*
     * Bincode 1.x wire format for `bytes::Bytes`, `Vec<u8>`, and `&[u8]`:
     * a u64 little-endian length followed by the raw bytes.
     */
    void encode_byte_slice(const void* data, size_t len) {
        encode_u64(len);
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        buffer_.insert(buffer_.end(), bytes, bytes + len);
    }

private:
    std::vector<uint8_t> buffer_;
};

/**
 * Bincode decoder for Crucible protocol.
 */
class Decoder {
public:
    Decoder(const std::vector<uint8_t>& data)
        : data_(data), pos_(0) {}

    Decoder(const uint8_t* data, size_t len)
        : data_(data, data + len), pos_(0) {}

    /**
     * Get current position.
     */
    size_t position() const { return pos_; }

    /**
     * Get remaining bytes.
     */
    size_t remaining() const { return data_.size() - pos_; }

    /**
     * Check if at end.
     */
    bool at_end() const { return pos_ >= data_.size(); }

    // Primitive types

    uint8_t decode_u8() {
        if (pos_ + 1 > data_.size()) {
            throw std::runtime_error("Decode error: not enough data for u8");
        }
        return data_[pos_++];
    }

    uint16_t decode_u16() {
        if (pos_ + 2 > data_.size()) {
            throw std::runtime_error("Decode error: not enough data for u16");
        }
        uint16_t val = data_[pos_] |
                      (static_cast<uint16_t>(data_[pos_ + 1]) << 8);
        pos_ += 2;
        return val;
    }

    uint32_t decode_u32() {
        if (pos_ + 4 > data_.size()) {
            throw std::runtime_error("Decode error: not enough data for u32");
        }
        uint32_t val = data_[pos_] |
                      (static_cast<uint32_t>(data_[pos_ + 1]) << 8) |
                      (static_cast<uint32_t>(data_[pos_ + 2]) << 16) |
                      (static_cast<uint32_t>(data_[pos_ + 3]) << 24);
        pos_ += 4;
        return val;
    }

    uint64_t decode_u64() {
        if (pos_ + 8 > data_.size()) {
            throw std::runtime_error("Decode error: not enough data for u64");
        }
        uint64_t val = 0;
        for (int i = 0; i < 8; i++) {
            val |= static_cast<uint64_t>(data_[pos_ + i]) << (i * 8);
        }
        pos_ += 8;
        return val;
    }

    bool decode_bool() {
        uint8_t val = decode_u8();
        return val != 0;
    }

    // Crucible types

    /*
     * Decode a UUID written by bincode's serialize_bytes: a u64 length
     * prefix (must be 16) followed by 16 raw bytes.
     */
    Uuid decode_uuid() {
        uint64_t len = decode_u64();
        if (len != 16) {
            throw std::runtime_error("Decode error: UUID byte-length is not 16");
        }
        if (pos_ + 16 > data_.size()) {
            throw std::runtime_error("Decode error: not enough data for UUID");
        }
        Uuid uuid;
        std::memcpy(uuid.bytes, &data_[pos_], 16);
        pos_ += 16;
        return uuid;
    }

    EncryptionContext decode_encryption_context() {
        if (pos_ + 28 > data_.size()) {
            throw std::runtime_error("Decode error: not enough data for EncryptionContext");
        }
        EncryptionContext ctx;
        std::memcpy(ctx.nonce, &data_[pos_], 12);
        pos_ += 12;
        std::memcpy(ctx.tag, &data_[pos_], 16);
        pos_ += 16;
        return ctx;
    }

    // Option<T>
    template<typename T, typename DecodeFunc>
    optional<T> decode_option(DecodeFunc decode_fn) {
        uint8_t tag = decode_u8();
        if (tag == 0) {
            return nullopt;  // None
        } else if (tag == 1) {
            return optional<T>(decode_fn());  // Some
        } else {
            throw std::runtime_error("Invalid Option tag: " + std::to_string(tag));
        }
    }

    // Vec<T>
    template<typename T, typename DecodeFunc>
    std::vector<T> decode_vec(DecodeFunc decode_fn) {
        uint64_t len = decode_u64();
        std::vector<T> vec;
        vec.reserve(len);
        for (uint64_t i = 0; i < len; i++) {
            vec.push_back(decode_fn());
        }
        return vec;
    }

    // String (Vec<u8>)
    std::string decode_string() {
        uint64_t len = decode_u64();
        if (pos_ + len > data_.size()) {
            throw std::runtime_error("Decode error: not enough data for string");
        }
        std::string str(reinterpret_cast<const char*>(&data_[pos_]), len);
        pos_ += len;
        return str;
    }

    // Raw bytes (caller already knows the length).
    std::vector<uint8_t> decode_bytes(size_t len) {
        if (pos_ + len > data_.size()) {
            throw std::runtime_error("Decode error: not enough data for bytes");
        }
        std::vector<uint8_t> bytes(data_.begin() + pos_, data_.begin() + pos_ + len);
        pos_ += len;
        return bytes;
    }

    /*
     * Read the u64 length prefix only, leaving the bytes themselves in the
     * stream.  Used when the bytes are large and we want to read them
     * directly into a caller-supplied buffer without copying.
     */
    uint64_t decode_byte_slice_length() {
        return decode_u64();
    }

    // Skip bytes
    void skip(size_t len) {
        if (pos_ + len > data_.size()) {
            throw std::runtime_error("Decode error: cannot skip beyond end");
        }
        pos_ += len;
    }

private:
    std::vector<uint8_t> data_;
    size_t pos_;
};

} // namespace bincode
} // namespace crucible

#endif // CRUCIBLE_BINCODE_HH
