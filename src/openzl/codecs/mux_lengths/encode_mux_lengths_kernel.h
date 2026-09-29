// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef OPENZL_CODECS_MUX_LENGTHS_ENCODE_MUX_LENGTHS_KERNEL_H
#define OPENZL_CODECS_MUX_LENGTHS_ENCODE_MUX_LENGTHS_KERNEL_H

#include <stddef.h>

#include "openzl/shared/portability.h"
#include "openzl/zl_errors.h"

ZL_BEGIN_C_DECLS

/// Need at least this many elements
#define ZL_MUX_LONG_SLOP_ELTS 128

/// Encode literal lengths and match lengths into muxed bytes with overflow.
///
/// @param muxedOut       Output buffer for muxed bytes (numElements capacity).
/// @param longOut        Output buffer for overflow lengths, interleaved.
///                       MUST be large enough to contain at least
///                       `ZL_muxLengthsCountLong() + ZL_MUX_LONG_SLOP_ELTS`
///                       elements.
/// @param literalLengths Input literal lengths (numElements elements).
/// @param matchLengths   Input match lengths (numElements elements).
/// @param numElements    Number of length pairs.
/// @param eltWidth       Element width in bytes (1, 2, 4, or 8).
/// @param splitPoint     Number of bits for literal lengths in muxed byte
///                       [0-8].
/// @param matchLengthBias Bias subtracted from match lengths before muxing
///                       using wrapping unsigned arithmetic [0-15].
size_t ZL_muxLengthsEncode(
        uint8_t* muxedOut,
        void* longOut,
        const void* literalLengths,
        const void* matchLengths,
        size_t numElements,
        size_t eltWidth,
        unsigned splitPoint,
        unsigned matchLengthBias);

/// @returns The number of overflow (long) lengths that will be produced by
/// encoding, which can be used to pre-size the long output buffer before
/// calling ZL_muxLengthsEncode().
size_t ZL_muxLengthsCountLong(
        const void* literalLengths,
        const void* matchLengths,
        size_t numElements,
        size_t eltWidth,
        unsigned splitPoint,
        unsigned matchLengthBias);

/// Result of computing the optimal mux_lengths split point.
typedef struct {
    size_t splitPoint; ///< Number of bits for literal lengths in each muxed
                       ///< byte
    size_t numLong;    ///< Number of overflow (long) lengths
} ZL_MuxLengthsSplitResult;

/// Compute the optimal split point (number of bits for literal lengths) for
/// mux_lengths by minimizing total overflow count across all possible splits.
/// Also returns the number of overflow (long) elements for the chosen split.
///
/// @param literalLengths Input literal lengths (numElements elements).
/// @param matchLengths   Input match lengths (numElements elements).
/// @param numElements    Number of length pairs.
/// @param eltWidth       Element width in bytes (1, 2, 4, or 8).
/// @param matchLengthBias Bias subtracted from match lengths [0-15].
ZL_MuxLengthsSplitResult ZL_muxLengthsComputeSplitPoint(
        const void* literalLengths,
        const void* matchLengths,
        size_t numElements,
        size_t eltWidth,
        unsigned matchLengthBias);

ZL_END_C_DECLS

#endif
