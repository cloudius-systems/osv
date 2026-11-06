/*
 * Copyright (C) 2024 Waldemar Kozaczuk
 * Copyright (C) 2024 OSv Contributors
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef CRUCIBLE_HASH_HH
#define CRUCIBLE_HASH_HH

#include <cstdint>
#include <cstddef>

namespace crucible {

/**
 * xxHash64 implementation for Crucible block integrity.
 *
 * This is a simplified implementation of xxHash64 for block hashing.
 * For production, consider using the official xxHash library.
 *
 * Based on the xxHash64 algorithm by Yann Collet.
 */
class XXHash64 {
public:
    /**
     * Compute xxHash64 of data.
     *
     * @param data Data to hash
     * @param len Length of data
     * @param seed Seed value (use 0 for Crucible)
     * @return 64-bit hash value
     */
    static uint64_t hash(const void* data, size_t len, uint64_t seed = 0);

private:
    static constexpr uint64_t PRIME64_1 = 0x9E3779B185EBCA87ULL;
    static constexpr uint64_t PRIME64_2 = 0xC2B2AE3D27D4EB4FULL;
    static constexpr uint64_t PRIME64_3 = 0x165667B19E3779F9ULL;
    static constexpr uint64_t PRIME64_4 = 0x85EBCA77C2B2AE63ULL;
    static constexpr uint64_t PRIME64_5 = 0x27D4EB2F165667C5ULL;

    static inline uint64_t rotl64(uint64_t x, int r) {
        return (x << r) | (x >> (64 - r));
    }

    static inline uint64_t round(uint64_t acc, uint64_t input) {
        acc += input * PRIME64_2;
        acc = rotl64(acc, 31);
        acc *= PRIME64_1;
        return acc;
    }

    static inline uint64_t merge_accumulator(uint64_t acc, uint64_t acc_n) {
        acc ^= round(0, acc_n);
        acc = acc * PRIME64_1 + PRIME64_4;
        return acc;
    }
};

/**
 * Compute xxHash64 of a block (convenience function).
 *
 * @param block Block data
 * @param block_size Block size in bytes
 * @return 64-bit hash value
 */
inline uint64_t xxhash64_block(const void* block, size_t block_size) {
    return XXHash64::hash(block, block_size, 0);
}

} // namespace crucible

#endif // CRUCIBLE_HASH_HH
