// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_DISPATCHN_BYTAG_DECODE_DISPATCHN_BYTAG_BINDING_H
#define ZSTRONG_TRANSFORMS_DISPATCHN_BYTAG_DECODE_DISPATCHN_BYTAG_BINDING_H

#include "openzl/codecs/dispatchN_byTag/graph_dispatchN_byTag.h" // GRAPH_DIPATCHNBYTAG
#include "openzl/shared/portability.h"
#include "openzl/zl_dtransform.h" // ZL_Decoder*

ZL_BEGIN_C_DECLS

/* DI_dispatchN_byTag():
 * Conditions :
 * - dictx is valid
 * - nbInSingletons == 2
 * - both inSingleton[] are type ZL_Type_numeric
 * - all inVO[] are type ZL_Type_serial
 */
ZL_Report DI_dispatchN_byTag(
        ZL_Decoder* dictx,
        const ZL_Input* inSingleton[],
        size_t nbInSingletons,
        const ZL_Input* inVO[],
        size_t nbInVOs);

#define DI_DIPATCHNBYTAG(id) \
    { .transform_f = DI_dispatchN_byTag, .name = "decode_dispatchN_byTag" }

ZL_END_C_DECLS

#endif
