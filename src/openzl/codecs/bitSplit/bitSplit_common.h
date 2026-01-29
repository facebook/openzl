// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_BITSPLIT_COMMON_H
#define ZSTRONG_TRANSFORMS_BITSPLIT_COMMON_H

#include <stddef.h>

#include "openzl/zl_portability.h"

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * Determines the output element width for a given bit width.
 *
 * @param bitWidth Number of bits (1-64)
 * @return Output element width in bytes (1, 2, 4, or 8)
 */
size_t ZS_bitSplit_outputEltWidth(unsigned bitWidth);

#if defined(__cplusplus)
}
#endif

#endif // ZSTRONG_TRANSFORMS_BITSPLIT_COMMON_H
