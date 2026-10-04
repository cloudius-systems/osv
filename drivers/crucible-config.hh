/* Copyright (C) 2026 OSv Contributors. BSD license; see LICENSE. */
/* Copyright (C) 2026 Greg Burd. */
#ifndef CRUCIBLE_CONFIG_HH
#define CRUCIBLE_CONFIG_HH
#include <cstdint>
#include <string>
#include <stdexcept>
namespace crucible {
// Operator-owned fencing authority, not an automatically elected epoch.
inline uint64_t parse_generation(const std::string& value)
{
    if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos) {
        throw std::invalid_argument("Crucible generation must be nonzero decimal uint64");
    }
    auto generation = std::stoull(value);
    if (!generation) {
        throw std::invalid_argument("Crucible generation must be nonzero");
    }
    return generation;
}
}
#endif
