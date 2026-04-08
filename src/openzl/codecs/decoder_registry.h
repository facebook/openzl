// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_DECODER_REGISTRY_H
#define ZSTRONG_TRANSFORMS_DECODER_REGISTRY_H

#include "openzl/common/wire_format.h" // ZL_StandardTransformID_end
#include "openzl/decompress/decoder_fusion.h"
#include "openzl/decompress/dtransforms.h" // DTransform, DTrDesc
#include "openzl/shared/portability.h"

ZL_BEGIN_C_DECLS

typedef struct {
    DTransform dtr;
    unsigned minFormatVersion;
    unsigned maxFormatVersion;
} StandardDTransform;

extern const StandardDTransform SDecoders_array[ZL_StandardTransformID_end];

/// IDs for the built-in decoder fusions. New fusions should be added before
/// ZL_DecoderFusionID_end.
typedef enum {
    ZL_DecoderFusionID_end,
} ZL_DecoderFusionID;

/// The built-in decoder fusion descriptors, indexed by ZL_DecoderFusionID.
/// TODO: Remove the +1 once ZL_DecoderFusion_end != 0.
/// It is needed because zero-lengthed arrays are not standard C.
extern const ZL_DecoderFusionDesc
        ZL_DecoderFusion_array[ZL_DecoderFusionID_end + 1];

ZL_END_C_DECLS

#endif
