// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_CODECS_BITSPLIT_FP32_H
#define OPENZL_CODECS_BITSPLIT_FP32_H

#include "openzl/zl_nodes.h"

#if defined(__cplusplus)
extern "C" {
#endif

// bitsplit transforms
// Input : 1 numeric stream (32 bit float)
// Output : 3 numeric streams (sign, exponent, mantissa)

#define ZL_NODE_BITSPLIT_FP32 ZL_MAKE_NODE_ID(ZL_StandardNodeID_bitsplit_fp32)

#if defined(__cplusplus)
}
#endif

#endif
