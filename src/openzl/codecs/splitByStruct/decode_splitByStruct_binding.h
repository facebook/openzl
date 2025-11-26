// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_SPLITBYSTRUCT_DECODE_SPLITBYSTRUCT_BINDING_H
#define ZSTRONG_TRANSFORMS_SPLITBYSTRUCT_DECODE_SPLITBYSTRUCT_BINDING_H

#include "openzl/codecs/splitByStruct/graph_splitByStruct.h" // GRAPH_SPLITBYSTRUCT_VO
#include "openzl/shared/portability.h"
#include "openzl/zl_dtransform.h" // ZL_Decoder*

ZL_BEGIN_C_DECLS

/* DI_splitByStruct():
 * Reverses EI_splitByStruct operation:
 * join fields from multiple input streams of type ZL_Type_struct
 * into a single array of structures of type ZL_Type_serial.

 * Conditions :
 * - dictx is valid
 * - nbInFixed == 0, inFixed is ignored
 * - nbInVariable > 0, and therefore inVariable is valid (!= NULL)
 * - all input streams are types ZL_Type_struct
 * - all input streams have same nb of elts
 */
ZL_Report DI_splitByStruct(
        ZL_Decoder* dictx,
        const ZL_Input* inFixed[],
        size_t nbInFixed,
        const ZL_Input* inVariable[],
        size_t nbInVariable);

#define DI_SPLITBYSTRUCT(id) \
    { .transform_f = DI_splitByStruct, .name = "structure transposition" }

ZL_END_C_DECLS

#endif
