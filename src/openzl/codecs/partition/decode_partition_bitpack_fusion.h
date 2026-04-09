// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef OPENZL_CODECS_PARTITION_DECODE_PARTITION_BITPACK_FUSION_H
#define OPENZL_CODECS_PARTITION_DECODE_PARTITION_BITPACK_FUSION_H

#include "openzl/decompress/decoder_fusion.h"
#include "openzl/shared/portability.h"

ZL_BEGIN_C_DECLS

/// Fused decoder for partition + bitpack_int.
/// The bitpack codec (child) produces the bucket IDs that feed into
/// partition input 0. Partition input 1 (offsets) is not fused.
ZL_Report ZL_partitionBitpackFusedDecode(ZL_DecoderFusion* state);

ZL_END_C_DECLS

#endif
