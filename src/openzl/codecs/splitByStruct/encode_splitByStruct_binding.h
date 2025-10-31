// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_SPLITBYSTRUCT_ENCODE_SPLITBYSTRUCT_BINDING_H
#define ZSTRONG_TRANSFORMS_SPLITBYSTRUCT_ENCODE_SPLITBYSTRUCT_BINDING_H

#include "openzl/codecs/splitByStruct/graph_splitByStruct.h" // GRAPH_SPLITBYSTRUCT_VO
#include "openzl/shared/portability.h"
#include "openzl/zl_ctransform.h" // ZL_Encoder

ZL_BEGIN_C_DECLS

#if 0
// For information : already declared in zs2_public_nodes.h

ZL_GraphID ZL_Compressor_registerSplitByStructGraph(
        ZL_Compressor* cgraph,
        const size_t* fieldSizes,
        const ZL_GraphID* successors,
        size_t nbFields);
#endif

/* The SplitByStruct operation is achieved by the combination of 2 nodes,
 * the first is a VO splitting transform,
 * and the second is a selector,
 * which assigns a specific outcome to each produced output.
 * The coordination between the 2 nodes is achieved by Stream Metadata.
 *
 * Both nodes are registered when invoking
 * ZL_Compressor_registerSplitByStructGraph().
 */

/* EI_splitByStruct() :
 * splits input content's into N output streams.
 * Instructions on how to split are transmitted through generic parameters
 * on paramID == ZL_SPLITBYSTRUCT_FIELDSIZES_PID .
 * Conditions :
 * - Input must be valid and of type ZL_Type_serial
 * - The sum of field lengths => struct_size .
 *   Input size must be a strict multiple of struct_size
 * Produces : N outputs of type ZL_Type_struct.
 */
ZL_Report
EI_splitByStruct(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);

#define ZL_SPLITBYSTRUCT_FIELDSIZES_PID 387

#define EI_SPLITBYSTRUCT(id)                     \
    { .gd          = GRAPH_SPLITBYSTRUCT_VO(id), \
      .transform_f = EI_splitByStruct,           \
      .name        = "!zl.private.split_by_struct" }

/* Exposed for tests */
ZL_NodeID ZL_createNode_splitByStruct(
        ZL_Compressor* cgraph,
        const size_t* fieldSizes,
        size_t nbFields);

ZL_END_C_DECLS

#endif // ZSTRONG_TRANSFORMS_SPLITBYSTRUCT_ENCODE_SPLITBYSTRUCT_BINDING_H
