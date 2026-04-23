// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef OPENZL_CODECS_MUX_LENGTHS_DECODE_MUX_LENGTHS_BINDING_H
#define OPENZL_CODECS_MUX_LENGTHS_DECODE_MUX_LENGTHS_BINDING_H

#include "openzl/codecs/mux_lengths/graph_mux_lengths.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_dtransform.h"

ZL_BEGIN_C_DECLS

/// Decoder binding for the mux_lengths transform.
/// Reads the codec header, then decodes muxed bytes + overflow streams
/// back into literal lengths and match lengths.
ZL_Report DI_mux_lengths(ZL_Decoder* dictx, const ZL_Input* ins[]);

/// Macro to register the mux_lengths decoder transform.
#define DI_MUX_LENGTHS(id) \
    { .transform_f = DI_mux_lengths, .name = "!zl.mux_lengths" }

ZL_END_C_DECLS

#endif
