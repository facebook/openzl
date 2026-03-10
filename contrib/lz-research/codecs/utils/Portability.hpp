// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <stdint.h>

#include "openzl/shared/portability.h"

#if ZL_ARCH_X86_64
#    include <immintrin.h>
#endif

namespace openzl::lz::utils {

inline uint64_t bitDeposit(uint64_t src, uint64_t mask)
{
#if ZL_ARCH_X86_64
    return _pdep_u64(src, mask);
#else
    uint64_t result = 0;
    uint64_t srcBit = 1;
    while (mask != 0) {
        uint64_t lowestBit = mask & (-int64_t(mask));
        if (src & srcBit) {
            result |= lowestBit;
        }
        mask &= ~lowestBit;
        srcBit <<= 1;
    }
    return result;
#endif
}

inline uint32_t bitDeposit(uint32_t src, uint32_t mask)
{
#if ZL_ARCH_X86_64
    return _pdep_u32(src, mask);
#else
    return uint32_t(bitDeposit(uint64_t(src), uint64_t(mask)));
#endif
}

inline uint64_t bitExtract(uint64_t src, uint64_t mask)
{
#if ZL_ARCH_X86_64
    return _pext_u64(src, mask);
#else
    uint64_t result = 0;
    uint64_t dstBit = 1;
    while (mask != 0) {
        uint64_t lowestBit = mask & (-int64_t(mask));
        if (src & lowestBit) {
            result |= dstBit;
        }
        mask &= ~lowestBit;
        dstBit <<= 1;
    }
    return result;
#endif
}

} // namespace openzl::lz::utils
