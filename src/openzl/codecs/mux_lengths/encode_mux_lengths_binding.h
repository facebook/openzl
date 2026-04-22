// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_CODECS_MUX_LENGTHS_ENCODE_MUX_LENGTHS_BINDING_H
#define OPENZL_CODECS_MUX_LENGTHS_ENCODE_MUX_LENGTHS_BINDING_H

#include "openzl/codecs/mux_lengths/graph_mux_lengths.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_ctransform.h"

ZL_BEGIN_C_DECLS

/// Encoder binding for the mux_lengths transform.
/// Reads split_point and match_length_bias from local int params.
/// If split_point is not set, it is auto-computed to minimize overflows.
/// If match_length_bias is not set, it is read from the match length stream's
/// ZL_LZ_MIN_MATCH_LENGTH_METADATA_ID metadata, defaulting to 0.
ZL_Report
EI_mux_lengths(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);

/// Macro to register the mux_lengths encoder transform.
#define EI_MUX_LENGTHS(id)                  \
    { .gd          = MUX_LENGTHS_GRAPH(id), \
      .transform_f = EI_mux_lengths,        \
      .name        = "!zl.mux_lengths" }

ZL_END_C_DECLS

#endif
