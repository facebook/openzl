// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_TRANSPOSE_ENCODE_TRANSPOSE_BINDING_H
#define ZSTRONG_TRANSFORMS_TRANSPOSE_ENCODE_TRANSPOSE_BINDING_H

#include "openzl/codecs/common/graph_pipe.h"         // PIPE_GRAPH
#include "openzl/codecs/transpose/graph_transpose.h" // TRANSPOSE_GRAPH
#include "openzl/shared/portability.h"
#include "openzl/zl_ctransform.h" // ZL_Encoder
#include "openzl/zl_graph_api.h"
#include "openzl/zl_selector.h"

ZL_BEGIN_C_DECLS

// New Transpose operation,
// which automatically adjusts to token's size.
// Accepts and generates `ZL_Type_struct` stream type.
ZL_Report EI_transpose(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);
ZL_Report
EI_transpose_split(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);

#define EI_TRANSPOSE(id)                  \
    { .gd          = TRANSPOSE_GRAPH(id), \
      .transform_f = EI_transpose,        \
      .name        = "!zl.private.transpose_deprecated" }

#define EI_TRANSPOSE_SPLIT(id)                  \
    { .gd          = TRANSPOSE_GRAPH_SPLIT(id), \
      .transform_f = EI_transpose_split,        \
      .name        = "!zl.transpose_split" }

ZL_INLINE bool ZL_Selector_isTransposeSplitSupported(
        const ZL_Selector* selector)
{
    return ZL_Selector_getCParam(selector, ZL_CParam_formatVersion) >= 11;
}

ZL_INLINE bool ZL_Graph_isTransposeSplitSupported(const ZL_Graph* graph)
{
    return ZL_Graph_getCParam(graph, ZL_CParam_formatVersion) >= 11;
}

/* =============================================
 * LEGACY transforms
 * =============================================
 * preserved for backup purposes.
 * They are now considered deprecated,
 * and will be removed at some point in the future.
 * For newer graphs, prefer using above TRANSPOSE transform.
 */

/* Design Note :
 *
 * These transforms expect an input as serialized format,
 * which they then interpret as specified (2, 4 or 8 bytes tokens),
 * and then produce an output as serialized format.
 */

/* use new TypedTransform interface */
ZL_Report EI_transpose_2bytes_typed(
        ZL_Encoder* eictx,
        const ZL_Input* ins[],
        size_t nbIns);
ZL_Report EI_transpose_4bytes_typed(
        ZL_Encoder* eictx,
        const ZL_Input* ins[],
        size_t nbIns);
ZL_Report EI_transpose_8bytes_typed(
        ZL_Encoder* eictx,
        const ZL_Input* ins[],
        size_t nbIns);

ZL_Report EI_transpose_split2bytes(
        ZL_Encoder* eictx,
        const ZL_Input* ins[],
        size_t nbIns);
ZL_Report EI_transpose_split4bytes(
        ZL_Encoder* eictx,
        const ZL_Input* ins[],
        size_t nbIns);
ZL_Report EI_transpose_split8bytes(
        ZL_Encoder* eictx,
        const ZL_Input* ins[],
        size_t nbIns);

#define EI_TRANSPOSE_2(id)                      \
    { .gd          = PIPE_GRAPH(id),            \
      .transform_f = EI_transpose_2bytes_typed, \
      .name        = "!zl.private.transpose2_deprecated" }

#define EI_TRANSPOSE_4(id)                      \
    { .gd          = PIPE_GRAPH(id),            \
      .transform_f = EI_transpose_4bytes_typed, \
      .name        = "!zl.private.transpose4_deprecated" }

#define EI_TRANSPOSE_8(id)                      \
    { .gd          = PIPE_GRAPH(id),            \
      .transform_f = EI_transpose_8bytes_typed, \
      .name        = "!zl.private.transpose8_deprecated" }

#define EI_TRANSPOSE_SPLIT2(id)                  \
    { .gd          = TRANSPOSE_GRAPH_SPLIT2(id), \
      .transform_f = EI_transpose_split2bytes,   \
      .name        = "!zl.private.transpose_split2_deprecated" }

#define EI_TRANSPOSE_SPLIT4(id)                  \
    { .gd          = TRANSPOSE_GRAPH_SPLIT4(id), \
      .transform_f = EI_transpose_split4bytes,   \
      .name        = "!zl.private.transpose_split4_deprecated" }

#define EI_TRANSPOSE_SPLIT8(id)                  \
    { .gd          = TRANSPOSE_GRAPH_SPLIT8(id), \
      .transform_f = EI_transpose_split8bytes,   \
      .name        = "!zl.private.transpose_split8_deprecated" }

/* old methods, suitable for PipeTransform,
 * no longer used, just for reference */
size_t EI_transpose_2bytes(
        void* dst,
        size_t dstCapacity,
        const void* src,
        size_t srcSize);
size_t EI_transpose_4bytes(
        void* dst,
        size_t dstCapacity,
        const void* src,
        size_t srcSize);
size_t EI_transpose_8bytes(
        void* dst,
        size_t dstCapacity,
        const void* src,
        size_t srcSize);

ZL_END_C_DECLS

#endif // ZSTRONG_TRANSFORMS_TRANSPOSE_ENCODE_TRANSPOSE_BINDING_H
