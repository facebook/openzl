// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef OPENZL_CODECS_MUX_LENGTHS_DECODE_MUX_LENGTHS_KERNEL_H
#define OPENZL_CODECS_MUX_LENGTHS_DECODE_MUX_LENGTHS_KERNEL_H

#include <stddef.h>
#include <stdint.h>

/// Detailed error codes for mux lengths decoding.
typedef enum {
    ZL_MuxLengthsError_ok = 0,
    ZL_MuxLengthsError_longLengthsExhausted,
    ZL_MuxLengthsError_longLengthsNotConsumed,
} ZL_MuxLengthsError;

/// Decode muxed length bytes back into literal lengths and match lengths.
///
/// @param literalLengthsOut  Output buffer for literal lengths
///                           (numMuxed * eltWidth capacity).
/// @param matchLengthsOut    Output buffer for match lengths
///                           (numMuxed * eltWidth capacity).
/// @param muxedLengths       Input muxed byte stream.
/// @param numMuxed           Number of muxed bytes.
/// @param longLengths        Input overflow lengths (interleaved).
/// @param numLong            Number of overflow length values.
/// @param eltWidth           Element width in bytes (1, 2, 4, or 8).
/// @param splitPoint         Number of bits for literal lengths [0-8].
/// @param matchLengthBias     Bias added back to match lengths.
///
/// @returns ZL_MuxLengthsError_ok on success, or an error code upon
///          corruption.
ZL_MuxLengthsError ZL_muxLengthsDecode(
        void* literalLengthsOut,
        void* matchLengthsOut,
        const uint8_t* muxedLengths,
        size_t numMuxed,
        const void* longLengths,
        size_t numLong,
        size_t eltWidth,
        unsigned splitPoint,
        unsigned matchLengthBias);

#endif
