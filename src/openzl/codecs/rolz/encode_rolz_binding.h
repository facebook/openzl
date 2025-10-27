// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_ROLZ_ENCODE_ROLZ_BINDING_H
#define ZSTRONG_TRANSFORMS_ROLZ_ENCODE_ROLZ_BINDING_H

#include "openzl/codecs/common/graph_pipe.h" // PIPE_GRAPH
#include "openzl/shared/portability.h"
#include "openzl/zl_ctransform.h" // eictx

ZL_BEGIN_C_DECLS

/* new methods, based on typedTransform */
ZL_Report EI_rolz_typed(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);
ZL_Report
EI_fastlz_typed(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);

// Following ZL_TypedEncoderDesc declaration,
// presumed to be used as initializer only
#define EI_ROLZ(id)                  \
    { .gd          = PIPE_GRAPH(id), \
      .transform_f = EI_rolz_typed,  \
      .name        = "!zl.private.rolz_deprecated" }

#define EI_FASTLZ(id)                 \
    { .gd          = PIPE_GRAPH(id),  \
      .transform_f = EI_fastlz_typed, \
      .name        = "!zl.private.fast_lz_deprecated" }

/* =============================================
 * LEGACY transforms
 * =============================================
 * preserved for compatibility purposes.
 * They will likely be deprecated at some point in the future.
 */

/* older method, based on pipeTransform,
 * no longer used */

size_t EI_rolz_dstBound(const void* src, size_t srcSize);
size_t EI_rolz(void* dst, size_t dstCapacity, const void* src, size_t srcSize);

size_t EI_fastlz_dstBound(const void* src, size_t srcSize);
size_t
EI_fastlz(void* dst, size_t dstCapacity, const void* src, size_t srcSize);

ZL_END_C_DECLS

#endif // ZSTRONG_TRANSFORMS_ROLZ_ENCODE_ROLZ_BINDING_H
