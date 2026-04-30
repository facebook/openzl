// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_SPLITN_ENCODE_SPLITN_BINDING_H
#define ZSTRONG_TRANSFORMS_SPLITN_ENCODE_SPLITN_BINDING_H

#include "openzl/codecs/common/graph_vo.h" // GRAPH_VO_*
#include "openzl/shared/portability.h"
#include "openzl/zl_errors.h"       // ZL_Report
#include "openzl/zl_opaque_types.h" // ZL_Encoder

ZL_BEGIN_C_DECLS

#if 0 // Avoid warnings emitted by -Wredundant-decls
// Already declared in zs2_public_nodes.h :
ZL_NodeID ZL_Compressor_registerSplitNode_withParams(
        ZL_Compressor* cgraph,
        const size_t* segmentSizes,
        size_t nbSegments);

ZL_NodeID ZL_Compressor_registerSplitNode_withParser(
        ZL_Compressor* cgraph,
        ZL_SplitParserFn f,
        void const* opaque);

ZL_GraphID ZL_Compressor_registerSplitGraph(
        ZL_Compressor* cgraph,
        const size_t segmentSizes[],
        const ZL_GraphID successors[],
        size_t nbSegments);
#endif

/* EI_splitN():
 * splits input content's into N output streams
 * (note: technically, just references each segment directly within @in).
 * Instructions on how to split are transmitted through generic parameters,
 * either paramID = ZL_SPLITN_PARSINGF_PID (priority)
 * or paramID == ZL_SPLITN_SEGMENTSIZES_PARAMID .
 * Conditions :
 * - Input must be valid and of type ZL_Type_serial
 * - The sum of lengths provided by parameters must be == input.size
 *   + Exception : last segment size is allowed to be set to `0`,
 *                 in which case it means "whatever is left within input".
 *                 In this scenario, the sum of lengths must be <= input.size
 * - Input can be empty, in which case the sum of lengths must be == 0.
 */
ZL_Report EI_splitN(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);

#define EI_SPLITN(id)                     \
    { .gd          = GRAPH_VO_SERIAL(id), \
      .transform_f = EI_splitN,           \
      .name        = "!zl.private.splitN" }

#define EI_SPLITN_STRUCT(id)              \
    { .gd          = GRAPH_VO_STRUCT(id), \
      .transform_f = EI_splitN,           \
      .name        = "!zl.private.splitN_struct" }

#define EI_SPLITN_NUM(id)              \
    { .gd          = GRAPH_VO_NUM(id), \
      .transform_f = EI_splitN,        \
      .name        = "!zl.private.splitN_num" }

ZL_END_C_DECLS

#endif // ZSTRONG_TRANSFORMS_SPLITN_ENCODE_SPLITN_BINDING_H
