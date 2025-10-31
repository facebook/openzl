// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_SPLITN_DECODE_SPLITN_BINDING_H
#define ZSTRONG_TRANSFORMS_SPLITN_DECODE_SPLITN_BINDING_H

#include "openzl/codecs/common/graph_vo.h" // GRAPH_VO_SERIAL
#include "openzl/shared/portability.h"
#include "openzl/zl_dtransform.h" // ZL_Decoder*

ZL_BEGIN_C_DECLS

/* DI_splitN():
 * Reverses EI_splitN operation.
 * Conditions :
 * - dictx is valid
 * - nbInFixed == 0 (inFixed is ignored)
 * - nbInVariable > 0, and therefore inVariable is valid (!= NULL)
 * - all input streams are types ZL_Type_serial
 */
ZL_Report DI_splitN(
        ZL_Decoder* dictx,
        const ZL_Input* inFixed[],
        size_t nbInFixed,
        const ZL_Input* inVariable[],
        size_t nbInVariable);

#define DI_SPLITN(id) { .transform_f = DI_splitN, .name = "splitN" }

#define DI_SPLITN_STRUCT(id) \
    { .transform_f = DI_splitN, .name = "splitN struct" }

#define DI_SPLITN_NUM(id) { .transform_f = DI_splitN, .name = "splitN num" }

ZL_END_C_DECLS

#endif // ZSTRONG_TRANSFORMS_SPLITN_DECODE_SPLITN_BINDING_H
