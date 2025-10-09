// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef ZSTRONG_TRANSFORMS_QUANTIZE_ENCODE_QUANTIZE_KERNEL_H
#define ZSTRONG_TRANSFORMS_QUANTIZE_ENCODE_QUANTIZE_KERNEL_H

#include "openzl/codecs/quantize/common_quantize.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_errors.h"

ZL_BEGIN_C_DECLS

/**
 * Quantizes the src using the quantization scheme described in `params`.
 * The values in the source are broken up into 2 parts: the codes and the extra
 * bits. See `ZL_Quantize32Params` docs for details.
 * Note that delta must ensure that no two values get mapped to the same code,
 * and a code should not be skipped. It should be: #(codes for values <
 * maxPow2) - log2(maxPow2).
 *
 * @param bits The output bits buffer.
 * @param bitsCapacity The capacity of the bits buffer.
 * @param codes The output codes buffer, must be at least `srcSize` bytes.
 * @param src The 32-bit values to quantize.
 * @param srcSize The number of input values.
 * @returns The size of the `bits` stream, or an error if the `bitsCapacity` is
 * too small.
 */
ZL_Report ZL_quantize32Encode(
        uint8_t* bits,
        size_t bitsCapacity,
        uint8_t* codes,
        uint32_t const* src,
        size_t srcSize,
        ZL_Quantize32Params const* params);

ZL_END_C_DECLS

#endif
