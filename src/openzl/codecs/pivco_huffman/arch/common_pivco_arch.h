// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef OPENZL_CODECS_PIVCO_HUFFMAN_ARCH_COMMON_PIVCO_ARCH_H
#define OPENZL_CODECS_PIVCO_HUFFMAN_ARCH_COMMON_PIVCO_ARCH_H

#include "openzl/shared/portability.h"

ZL_BEGIN_C_DECLS

/**
 * Slop bytes that every bitmap and rank buffer must reserve past its logical
 * length. The encode and decode kernels may over-read and over-write up to this
 * many trailing bytes where noted in the docs as `SLOP`, so callers pad their
 * buffers by this amount. It is sized to the widest fixed-width group the SIMD
 * kernels process.
 */
#define ZL_PIVCO_HUFFMAN_SLOP 64

ZL_END_C_DECLS

#endif
