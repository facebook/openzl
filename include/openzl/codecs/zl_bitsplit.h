// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_CODECS_BITSPLIT_H
#define OPENZL_CODECS_BITSPLIT_H

#include "openzl/zl_nodes.h"

#if defined(__cplusplus)
extern "C" {
#endif

// bitsplit transforms

// Input : 1 numeric stream (all widths supported: 1, 2, 4, 8 bytes)
// Output : 1 or 2 numeric streams (lower remainder bits, upper bits)
// Note : All variants share the same wire format (bitSplit).
//        Each variant bundles a different policy for choosing bit widths.
#define ZL_NODE_BITSPLIT_TOP8 ZL_MAKE_NODE_ID(ZL_StandardNodeID_bitsplit_top8)

// input  : 1 numeric stream (IEEE 754 widths: 2, 4, or 8 bytes)
// output : 1 variable output stream of 3 outputs
//          (mantissa, exponent, sign)
// Supports IEEE 754 fp16, fp32, fp64.
#define ZL_NODE_BITSPLIT_FP ZL_MAKE_NODE_ID(ZL_StandardNodeID_bitsplit_fp)

// input  : 1 numeric stream (2-byte bfloat16 elements)
// output : 1 variable output stream of 3 outputs
//          (mantissa, exponent, sign)
// bfloat16: sign (1 bit), exponent (8 bits), mantissa (7 bits).
#define ZL_NODE_BITSPLIT_BF16 ZL_MAKE_NODE_ID(ZL_StandardNodeID_bitsplit_bf16)

#if defined(__cplusplus)
}
#endif

#endif
