// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once
#include "openzl/shared/mem.h"

namespace openzl {
namespace protobuf {
namespace utils {

template <typename T>
T swap(const T& val) noexcept
{
    static_assert(
            sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4
            || sizeof(T) == 8);
    if (sizeof(T) == 1)
        return val;
    if (sizeof(T) == 2)
        return ZL_swap16(val);
    if (sizeof(T) == 4)
        return ZL_swap32(val);
    if (sizeof(T) == 8)
        return ZL_swap64(val);
}

template <typename T>
T toLE(const T& val)
{
    static_assert(
            std::is_integral<T>::value || std::is_floating_point<T>::value);
    return ZL_isLittleEndian() ? val : swap(val);
}

} // namespace utils
} // namespace protobuf
} // namespace openzl
