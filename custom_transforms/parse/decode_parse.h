// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef CUSTOM_TRANSFORMS_PARSE_DECODE_PARSE_H
#define CUSTOM_TRANSFORMS_PARSE_DECODE_PARSE_H

#include "openzl/shared/portability.h"
#include "openzl/zl_dtransform.h"

ZL_BEGIN_C_DECLS

/// Registers the parseInt64 transform with ID @p transformID.
/// See @p ZL_Compressor_registerParseInt64 for details.
extern "C" ZL_Report ZL_DCtx_registerParseInt64(
        ZL_DCtx* dctx,
        ZL_IDType transformID);

/// Registers the parseFloat64 transform with ID @p transformID.
/// See @p ZL_Compressor_registerParseFloat64 for details.
extern "C" ZL_Report ZL_DCtx_registerParseFloat64(
        ZL_DCtx* dctx,
        ZL_IDType transformID);

ZL_END_C_DECLS

#endif
