// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_CODECS_FLOAT_DECONSTRUCT_H
#define ZSTRONG_CODECS_FLOAT_DECONSTRUCT_H

#include "openzl/zl_nodes.h"

#if defined(__cplusplus)
extern "C" {
#endif

// Float deconstruct transforms
// Input : 1 numeric stream (width determined by the float type)
// Output : 1 fixed-size-fields stream containing sign and fraction bits,
//          1 serial stream containing exponent bits.
// Note : See zstrong/compress/transforms/float_deconstruct_encode.h for
// detailed description
//        of the bit layout for each transform.
#define ZL_NODE_FLOAT32_DECONSTRUCT \
    ZL_MAKE_NODE_ID(ZL_StandardNodeID_float32_deconstruct)
#define ZL_NODE_BFLOAT16_DECONSTRUCT \
    ZL_MAKE_NODE_ID(ZL_StandardNodeID_bfloat16_deconstruct)
#define ZL_NODE_FLOAT16_DECONSTRUCT \
    ZL_MAKE_NODE_ID(ZL_StandardNodeID_float16_deconstruct)

#if defined(__cplusplus)
}
#endif

#endif
