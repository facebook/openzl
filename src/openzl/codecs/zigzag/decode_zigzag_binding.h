// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_ZIGZAG_DECODE_ZIGZAG_BINDING_H
#define ZSTRONG_TRANSFORMS_ZIGZAG_DECODE_ZIGZAG_BINDING_H

#include "openzl/codecs/common/graph_pipe.h" // PIPE_GRAPH
#include "openzl/shared/portability.h"
#include "openzl/zl_dtransform.h" // ZL_Decoder

ZL_BEGIN_C_DECLS

/* Note :
 * Individual functions are exposed
 * in order to be used as part of macro initializers
 * which can then be considered "constant" by the compiler.
 **/

ZL_Report DI_zigzag_num(ZL_Decoder* dictx, const ZL_Input* in[]);

/* Note :
 * We use macros, instead of `extern const ZL_PipeDecoderDesc` variables,
 * because variables can't be used to initialize an array.
 */

// Following ZL_TypedEncoderDesc declaration,
// presumed to be used as initializer only
#define DI_ZIGZAG_NUM(id) { .transform_f = DI_zigzag_num, .name = "zigzag" }

ZL_END_C_DECLS

#endif // ZSTRONG_TRANSFORMS_ZIGZAG_DECODE_ZIGZAG_BINDING_H
