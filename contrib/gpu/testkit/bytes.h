// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "openzl/shared/mem.h"

// Conversions between values and the little-endian bytes of OpenZL serial
// streams. The byte order is explicit, so the bytes are the same on every
// host.

namespace openzl::gpu::testkit {

// Appends the `width` low bytes of `value`, least significant first. `width`
// is at most 8.
inline void appendLittleEndian(std::string& out, uint64_t value, size_t width)
{
    const size_t at = out.size();
    out.resize(at + width);
    ZL_writeLE64_N(out.data() + at, value, width);
}

// Returns the little-endian bytes of `values`.
template <typename T>
std::string toLittleEndianBytes(const std::vector<T>& values)
{
    static_assert(std::is_unsigned_v<T>, "T must be an unsigned integer");
    std::string out;
    out.reserve(values.size() * sizeof(T));
    for (const T value : values) {
        appendLittleEndian(out, value, sizeof(T));
    }
    return out;
}

// Returns the values that `bytes` holds in little-endian order. T is an
// unsigned integer or a float type. Trailing bytes that do not fill a value
// are ignored.
template <typename T>
std::vector<T> fromLittleEndianBytes(std::string_view bytes)
{
    static_assert(
            std::is_unsigned_v<T> || std::is_floating_point_v<T>,
            "T must be an unsigned integer or a float type");
    static_assert(sizeof(T) <= sizeof(uint64_t), "T must fit in 8 bytes");
    std::vector<T> values(bytes.size() / sizeof(T));
    for (size_t i = 0; i < values.size(); ++i) {
        const uint64_t bits =
                ZL_readLE64_N(bytes.data() + i * sizeof(T), sizeof(T));
        if constexpr (std::is_unsigned_v<T>) {
            values[i] = static_cast<T>(bits);
        } else if constexpr (sizeof(T) == sizeof(uint32_t)) {
            const auto narrow = static_cast<uint32_t>(bits);
            std::memcpy(&values[i], &narrow, sizeof(T));
        } else {
            std::memcpy(&values[i], &bits, sizeof(T));
        }
    }
    return values;
}

} // namespace openzl::gpu::testkit
