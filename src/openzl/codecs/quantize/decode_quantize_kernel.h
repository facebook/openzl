// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef ZSTRONG_TRANSFORMS_QUANTIZE_DECODE_QUANTIZE_KERNEL_H
#define ZSTRONG_TRANSFORMS_QUANTIZE_DECODE_QUANTIZE_KERNEL_H

#include "openzl/codecs/quantize/common_quantize.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_errors.h"

ZL_BEGIN_C_DECLS

/**
 * Decodes the quantized codes & bits using the quantization scheme described in
 * `params`. See `ZL_Quantize32Params` docs for details.
 *
 * @param dst The output values buffer. Must be large enough to fit `nbCodes`
 * values.
 * @param codes The quantized codes buffer.
 * @param nbCodes The number of quantized codes.
 * @param maxCode An upper bound on maximum code value in `codes`. The smaller
 * the value, the faster decoding can be, so a tight bound is best. If it is
 * >= params.nbCodes, the last code is used.
 * @param bits The bits buffer, where the extra bits for the codes are stored.
 * @param bitsSize The size of the bits buffer.
 * @returns Success or an error code upon failure.
 */
ZL_Report ZL_quantize32Decode(
        uint32_t* dst,
        uint8_t const* codes,
        size_t nbCodes,
        uint8_t maxCode,
        uint8_t const* bits,
        size_t bitsSize,
        ZL_Quantize32Params const* params);

ZL_END_C_DECLS

#endif
