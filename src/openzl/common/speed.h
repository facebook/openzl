// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_COMMON_DECODE_SPEED_H
#define ZSTRONG_COMMON_DECODE_SPEED_H

#include "openzl/shared/portability.h"

ZL_BEGIN_C_DECLS

/**
 * Speed:
 *
 * This library provides target [de]compression speeds, and allows
 * for a knob to tune decoding speed vs. compression ratio.
 *
 * TODO: This is not implemented yet, it is just here because I need
 * the structs in `entropy.h`. It will be get a baseline implementation
 * in a stacked diff.
 */

// slowest -> fastest
typedef enum  {
    ZL_DecodeSpeedBaseline_any     = 0, // No decoding speed constraints
    ZL_DecodeSpeedBaseline_zlib    = 1, // Aim for ZLIB speeds
    ZL_DecodeSpeedBaseline_zstd    = 2, // Aim for ZSTD speeds
    ZL_DecodeSpeedBaseline_lz4     = 3, // Aim for LZ4 speeds
    ZL_DecodeSpeedBaseline_fastest = 4, // Fastest possible decoding speed
} ZL_DecodeSpeedBaseline;

typedef enum {
    ZL_EncodeSpeedBaseline_any     = 0,
    ZL_EncodeSpeedBaseline_slower  = 1,
    ZL_EncodeSpeedBaseline_faster  = 2,
    ZL_EncodeSpeedBaseline_entropy = 3, // Allow entropy coding
    ZL_EncodeSpeedBaseline_fastest = 4, // Fastest possible
} ZL_EncodeSpeedBaseline;

typedef struct {
    ZL_EncodeSpeedBaseline baseline;
} ZL_EncodeSpeed;

typedef struct {
    /// Decoding speed baseline. Components should select modes that can meet
    /// the baseline decoding speed requirement.
    ZL_DecodeSpeedBaseline baseline;
    /// TODO: Allow a decoding speed vs. ratio tradeoff starting at baseline.
    /// Idea is: You can allow decoding speeds slower than the baselien if you
    /// gain enough. If the method is fast enough for baseline, you can always
    /// use it.
    int32_t _tradeoff;
} ZL_DecodeSpeed;

ZL_INLINE ZL_EncodeSpeed
ZL_EncodeSpeed_fromBaseline(ZL_EncodeSpeedBaseline baseline)
{
    ZL_EncodeSpeed speed = {
        .baseline = baseline,
    };
    return speed;
}

ZL_INLINE ZL_DecodeSpeed
ZL_DecodeSpeed_fromBaseline(ZL_DecodeSpeedBaseline baseline)
{
    ZL_DecodeSpeed speed = {
        .baseline  = baseline,
        ._tradeoff = 0,
    };
    return speed;
}

ZL_END_C_DECLS

#endif // ZSTRONG_COMMON_DECODE_SPEED_H
