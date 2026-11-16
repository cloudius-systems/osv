/*
 * Copyright (C) 2024 Waldemar Kozaczuk
 * Copyright (C) 2024 OSv Contributors
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "crucible-hash.hh"
#include <cstring>

namespace crucible {

uint64_t XXHash64::hash(const void* data, size_t len, uint64_t seed)
{
    const uint8_t* p = static_cast<const uint8_t*>(data);
    const uint8_t* const end = p + len;
    uint64_t h64;

    if (len >= 32) {
        const uint8_t* const limit = end - 32;
        uint64_t v1 = seed + PRIME64_1 + PRIME64_2;
        uint64_t v2 = seed + PRIME64_2;
        uint64_t v3 = seed + 0;
        uint64_t v4 = seed - PRIME64_1;

        do {
            uint64_t k1, k2, k3, k4;
            std::memcpy(&k1, p, 8); p += 8;
            std::memcpy(&k2, p, 8); p += 8;
            std::memcpy(&k3, p, 8); p += 8;
            std::memcpy(&k4, p, 8); p += 8;

            v1 = round(v1, k1);
            v2 = round(v2, k2);
            v3 = round(v3, k3);
            v4 = round(v4, k4);
        } while (p <= limit);

        h64 = rotl64(v1, 1) + rotl64(v2, 7) + rotl64(v3, 12) + rotl64(v4, 18);
        h64 = merge_accumulator(h64, v1);
        h64 = merge_accumulator(h64, v2);
        h64 = merge_accumulator(h64, v3);
        h64 = merge_accumulator(h64, v4);
    } else {
        h64 = seed + PRIME64_5;
    }

    h64 += len;

    // Process remaining bytes
    while (p + 8 <= end) {
        uint64_t k1;
        std::memcpy(&k1, p, 8);
        k1 *= PRIME64_2;
        k1 = rotl64(k1, 31);
        k1 *= PRIME64_1;
        h64 ^= k1;
        h64 = rotl64(h64, 27) * PRIME64_1 + PRIME64_4;
        p += 8;
    }

    if (p + 4 <= end) {
        uint32_t k1;
        std::memcpy(&k1, p, 4);
        h64 ^= static_cast<uint64_t>(k1) * PRIME64_1;
        h64 = rotl64(h64, 23) * PRIME64_2 + PRIME64_3;
        p += 4;
    }

    while (p < end) {
        h64 ^= (*p++) * PRIME64_5;
        h64 = rotl64(h64, 11) * PRIME64_1;
    }

    // Finalization
    h64 ^= h64 >> 33;
    h64 *= PRIME64_2;
    h64 ^= h64 >> 29;
    h64 *= PRIME64_3;
    h64 ^= h64 >> 32;

    return h64;
}

} // namespace crucible
