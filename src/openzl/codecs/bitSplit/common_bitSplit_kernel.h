// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_BITSPLIT_COMMON_H
#define ZSTRONG_TRANSFORMS_BITSPLIT_COMMON_H

#include <stddef.h>

/**
 * Determines the output element width for a given bit width.
 *
 * @param bitWidth Number of bits (1-64)
 * @return Output element width in bytes (1, 2, 4, or 8)
 */
size_t ZS_bitSplit_outputEltWidth(unsigned bitWidth);

#endif // ZSTRONG_TRANSFORMS_BITSPLIT_COMMON_H
