// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_ZIGZAG_ENCODE_ZIGZAG_BINDING_H
#define ZSTRONG_TRANSFORMS_ZIGZAG_ENCODE_ZIGZAG_BINDING_H

#include "openzl/codecs/common/graph_pipe.h" // PIPE_GRAPH
#include "openzl/shared/portability.h"
#include "openzl/zl_ctransform.h" // ZL_Encoder

ZL_BEGIN_C_DECLS

/* Numeric transform, employing a NUMPIPE graph (ZL_Type_numeric) */
ZL_Report EI_zigzag_num(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);

/* Note :
 * We use macros, instead of const variables,
 * because variables can't be used to initialize an array.
 * to be used as initializer only.
 */

#define EI_ZIGZAG_NUM(id)               \
    { .gd          = NUMPIPE_GRAPH(id), \
      .transform_f = EI_zigzag_num,     \
      .name        = "!zl.zigzag" }

ZL_END_C_DECLS

#endif // ZSTRONG_TRANSFORMS_ZIGZAG_ENCODE_ZIGZAG_BINDING_H
