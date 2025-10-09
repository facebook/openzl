// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef CUSTOM_TRANSFORMS_JSON_EXTRACT_DECODE_JSON_EXTRACT_H
#define CUSTOM_TRANSFORMS_JSON_EXTRACT_DECODE_JSON_EXTRACT_H

#include "openzl/shared/portability.h"
#include "openzl/zl_dtransform.h"

ZL_BEGIN_C_DECLS

/**
 * Registers JSON extract custom transform using the given @p transformID.
 * @see ZL_Compressor_registerJsonExtract for details.
 */
ZL_Report ZL_DCtx_registerJsonExtract(ZL_DCtx* dctx, ZL_IDType transformID);

ZL_END_C_DECLS

#endif
