// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_CODECS_BITSPLIT_COMMON_BITSPLIT_KERNEL_H
#define OPENZL_CODECS_BITSPLIT_COMMON_BITSPLIT_KERNEL_H

#include <stddef.h>

/**
 * Determines the output element width for a given bit width.
 *
 * @param bitWidth Number of bits (1-64)
 * @return Output element width in bytes (1, 2, 4, or 8)
 */
size_t ZL_bitSplit_outputEltWidth(unsigned bitWidth);

#endif // OPENZL_CODECS_BITSPLIT_COMMON_BITSPLIT_KERNEL_H
